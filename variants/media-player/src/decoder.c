/* decoder.c — reprodução de áudio/vídeo com as bibliotecas do FFmpeg.
 *
 * Um thread por arquivo: demux (libavformat), decodifica áudio (libavcodec),
 * converte para S16 estéreo com libswresample e escreve no ALSA (que bloqueia
 * e portanto define o ritmo). O vídeo é decodificado no mesmo thread, escalado
 * para o tamanho do canvas com libswscale e deixado num buffer duplo; a UI
 * pega o quadro mais novo sem bloquear a decodificação.
 *
 * Sincronia: se há áudio, o relógio é o áudio (audio_clock); se não há, é o
 * relógio monotônico. Quadros que chegariam mais de 80 ms atrasados são
 * descartados (o RK3566 decodifica 720p em software, então atrasar acontece).
 *
 * Compatibilidade: usa só API presente no FFmpeg 5.1 (Debian bookworm 12, que é
 * o alvo da imagem) e nas versões novas (AVChannelLayout + swr_alloc_set_opts2),
 * para compilar tanto no aparelho quanto numa máquina de desenvolvimento.
 */

#include "wm.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <errno.h>
#include <pthread.h>

#define ATRASO_MAXIMO 0.080
#define AUDIO_SAMPLE_RATE 48000

struct Player {
    char arquivo[WM_PATH_MAX];
    AudioOut *audio;

    pthread_t thread;
    pthread_mutex_t lock;
    bool thread_ok;

    /* pedidos vindos da UI */
    volatile bool pediu_parar;
    volatile bool pediu_pausa;
    volatile bool pausado;
    volatile bool pediu_seek;
    volatile double seek_para;
    volatile int volume;
    volatile double base_posicao;   /* posição em que a reprodução começou */

    /* estado publicado */
    bool aberto;
    bool tocando;
    bool terminou;
    bool tem_video;
    bool tem_audio;
    double duracao;
    double posicao;
    char titulo[192];
    char artista[192];
    char album[192];
    char erro[160];
    int largura;
    int altura;

    /* vídeo */
    uint32_t *quadros[2];
    int quadro_ativo;
    int quadro_w;
    int quadro_h;
    bool quadro_novo;
    bool quadro_valido;
    int video_max_w;
    int video_max_h;

    /* último erro fatal */
    bool falhou;
};

static void pub_erro(Player *p, const char *msg)
{
    pthread_mutex_lock(&p->lock);
    snprintf(p->erro, sizeof(p->erro), "%s", msg);
    p->falhou = true;
    p->tocando = false;
    pthread_mutex_unlock(&p->lock);
    wm_log("player: %s", msg);
}

static void copiar_tag(AVDictionary *dic, const char *chave, char *destino, size_t tam)
{
    AVDictionaryEntry *e = av_dict_get(dic, chave, NULL, AV_DICT_IGNORE_SUFFIX);

    if (e && e->value && e->value[0]) {
        snprintf(destino, tam, "%s", e->value);
        wm_trim(destino);
    }
}

/* "Artista - Título.mp3" quando o arquivo não tem tags. */
static void titulo_pelo_nome(const char *caminho, char *titulo, size_t tam_t,
                             char *artista, size_t tam_a)
{
    const char *base = strrchr(caminho, '/');
    char copia[256];
    char *sep;

    base = base ? base + 1 : caminho;
    snprintf(copia, sizeof(copia), "%s", base);
    {
        char *ponto = strrchr(copia, '.');
        if (ponto && ponto != copia) {
            *ponto = '\0';
        }
    }
    /* Remove "01 - " e afins. */
    {
        char *p = copia;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        if (p != copia && (*p == ' ' || *p == '-' || *p == '_' || *p == '.')) {
            while (*p == ' ' || *p == '-' || *p == '_' || *p == '.') {
                p++;
            }
            memmove(copia, p, strlen(p) + 1);
        }
    }
    sep = strstr(copia, " - ");
    if (sep) {
        *sep = '\0';
        snprintf(artista, tam_a, "%.*s", (int) tam_a - 1, copia);
        snprintf(titulo, tam_t, "%.*s", (int) tam_t - 1, sep + 3);
        wm_trim(titulo);
        wm_trim(artista);
    } else {
        snprintf(titulo, tam_t, "%.*s", (int) tam_t - 1, copia);
    }
}

static bool abrir_decodificador(AVFormatContext *fmt, int indice,
                               AVCodecContext **ctx_out)
{
    const AVCodec *codec;
    AVCodecContext *ctx;

    if (indice < 0) {
        return false;
    }
    codec = avcodec_find_decoder(fmt->streams[indice]->codecpar->codec_id);
    if (!codec) {
        wm_log("player: decodificador desconhecido para o stream %d", indice);
        return false;
    }
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        return false;
    }
    if (avcodec_parameters_to_context(ctx, fmt->streams[indice]->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    ctx->pkt_timebase = fmt->streams[indice]->time_base;
    /* Threads de decodificação: ajuda muito em 720p por software no RK3566
     * quando o codec suporta (o FFmpeg escolhe o modo por codec). */
    ctx->thread_count = 0;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    *ctx_out = ctx;
    return true;
}

typedef struct {
    AVFormatContext *fmt;
    int audio_idx;
    int video_idx;
    AVCodecContext *audio_ctx;
    AVCodecContext *video_ctx;
    SwrContext *swr;
    struct SwsContext *sws;
    int sws_w, sws_h;
    int sws_lw, sws_lh;
    int sws_fmt;
    AVFrame *frame;
    AVPacket *pkt;
} DecodeCtx;

static double stream_segundos(AVFormatContext *fmt, int idx, int64_t ts)
{
    AVRational tb = fmt->streams[idx]->time_base;
    double t = (double) ts * av_q2d(tb);

    return t;
}

static void configurar_swr(DecodeCtx *d, AVCodecContext *actx)
{
    AVChannelLayout estereo = AV_CHANNEL_LAYOUT_STEREO;

    swr_free(&d->swr);
    if (swr_alloc_set_opts2(&d->swr, &estereo, AV_SAMPLE_FMT_S16, AUDIO_SAMPLE_RATE,
                            &actx->ch_layout,
                            actx->sample_fmt,
                            actx->sample_rate > 0 ? actx->sample_rate : AUDIO_SAMPLE_RATE,
                            0, NULL) < 0) {
        wm_log("player: swr_alloc_set_opts2 falhou");
        return;
    }
    if (swr_init(d->swr) < 0) {
        wm_log("player: swr_init falhou");
        swr_free(&d->swr);
    }
}

static void escapar_e_converter(DecodeCtx *d, Player *p, AVFrame *frame)
{
    int capacidade;
    uint8_t *saida;
    uint8_t *ptrs[1];
    int amostras;

    if (!d->swr) {
        return;
    }
    capacidade = (int) av_rescale_rnd(swr_get_delay(d->swr, frame->sample_rate)
                                          + frame->nb_samples,
                                      AUDIO_SAMPLE_RATE, frame->sample_rate, AV_ROUND_UP);
    if (capacidade <= 0) {
        return;
    }
    saida = av_malloc((size_t) capacidade * 2 * sizeof(int16_t));
    if (!saida) {
        return;
    }
    ptrs[0] = saida;
    amostras = swr_convert(d->swr, ptrs, capacidade, (const uint8_t **) frame->extended_data,
                           frame->nb_samples);
    if (amostras > 0) {
        int volume = p->volume;
        if (volume != 100) {
            /* Volume por software além do mixer: garante 0% silencioso mesmo
             * quando o controle ALSA não chega a zero. */
            int16_t *s = (int16_t *) saida;
            for (int i = 0; i < amostras * 2; i++) {
                s[i] = (int16_t) ((int32_t) s[i] * volume / 100);
            }
        }
        audio_write(p->audio, (const int16_t *) saida, amostras);
        p->posicao = p->base_posicao + audio_clock(p->audio);
    }
    av_free(saida);
}

/* Converte o frame de vídeo para RGB32 no tamanho do canvas, com letterbox. */
static void preparar_quadro(DecodeCtx *d, Player *p, AVFrame *frame)
{
    int lw = frame->width;
    int lh = frame->height;
    int dw = p->video_max_w;
    int dh = p->video_max_h;
    double escala;
    int w, h, ox, oy;
    uint8_t *dst_data[4] = { NULL, NULL, NULL, NULL };
    int dst_lines[4] = { 0, 0, 0, 0 };
    int alvo;

    if (lw <= 0 || lh <= 0 || dw <= 0 || dh <= 0) {
        return;
    }
    escala = (double) dw / (double) lw;
    if ((double) lh * escala > dh) {
        escala = (double) dh / (double) lh;
    }
    w = (int) (lw * escala) & ~1;
    h = (int) (lh * escala) & ~1;
    if (w < 2) {
        w = 2;
    }
    if (h < 2) {
        h = 2;
    }
    if (w > dw) {
        w = dw & ~1;
    }
    if (h > dh) {
        h = dh & ~1;
    }
    ox = (dw - w) / 2;
    oy = (dh - h) / 2;

    alvo = p->quadro_ativo ^ 1;
    if (!p->quadros[alvo]) {
        p->quadros[alvo] = malloc((size_t) dw * (size_t) dh * sizeof(uint32_t));
        if (!p->quadros[alvo]) {
            return;
        }
    }
    /* Fundo preto no quadro inteiro (letterbox). */
    memset(p->quadros[alvo], 0, (size_t) dw * (size_t) dh * sizeof(uint32_t));

    if (!d->sws || d->sws_w != w || d->sws_h != h || d->sws_fmt != frame->format
        || d->sws_lw != lw || d->sws_lh != lh) {
        sws_freeContext(d->sws);
        d->sws = sws_getContext(lw, lh, (enum AVPixelFormat) frame->format,
                                w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
        d->sws_w = w;
        d->sws_h = h;
        d->sws_fmt = frame->format;
        d->sws_lw = lw;
        d->sws_lh = lh;
    }
    if (!d->sws) {
        return;
    }
    dst_data[0] = (uint8_t *) (p->quadros[alvo] + (size_t) oy * dw + ox);
    dst_lines[0] = dw * 4;
    sws_scale(d->sws, (const uint8_t *const *) frame->data, frame->linesize, 0, lh,
              dst_data, dst_lines);

    pthread_mutex_lock(&p->lock);
    p->quadro_ativo = alvo;
    p->quadro_novo = true;
    p->quadro_valido = true;
    p->quadro_w = dw;
    p->quadro_h = dh;
    p->largura = lw;
    p->altura = lh;
    pthread_mutex_unlock(&p->lock);
}

static void *thread_player(void *arg)
{
    Player *p = arg;
    DecodeCtx d;
    double inicio = 0.0;
    bool usou_audio = false;

    memset(&d, 0, sizeof(d));
    d.audio_idx = -1;
    d.video_idx = -1;

    if (avformat_open_input(&d.fmt, p->arquivo, NULL, NULL) < 0) {
        pub_erro(p, "nao consegui abrir o arquivo");
        return NULL;
    }
    if (avformat_find_stream_info(d.fmt, NULL) < 0) {
        pub_erro(p, "arquivo sem streams reconheciveis");
        avformat_close_input(&d.fmt);
        return NULL;
    }
    d.audio_idx = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    d.video_idx = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (d.audio_idx < 0) {
        d.audio_idx = -1;
    }
    if (d.video_idx < 0) {
        d.video_idx = -1;
    }
    /* Capa de álbum vem como stream de vídeo "attached_pic"; não é vídeo. */
    if (d.video_idx >= 0
        && (d.fmt->streams[d.video_idx]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        d.video_idx = -1;
    }

    if (d.audio_idx >= 0 && !abrir_decodificador(d.fmt, d.audio_idx, &d.audio_ctx)) {
        d.audio_idx = -1;
    }
    if (d.video_idx >= 0 && !abrir_decodificador(d.fmt, d.video_idx, &d.video_ctx)) {
        d.video_idx = -1;
    }
    if (d.audio_idx < 0 && d.video_idx < 0) {
        pub_erro(p, "sem audio nem video utilizavel");
        avformat_close_input(&d.fmt);
        return NULL;
    }
    if (d.audio_idx >= 0) {
        configurar_swr(&d, d.audio_ctx);
        if (!d.swr) {
            d.audio_idx = -1;
        }
    }

    d.frame = av_frame_alloc();
    d.pkt = av_packet_alloc();

    {
        double duracao = 0.0;
        if (d.fmt->duration > 0) {
            duracao = (double) d.fmt->duration / (double) AV_TIME_BASE;
        }
        pthread_mutex_lock(&p->lock);
        p->aberto = true;
        p->tem_audio = d.audio_idx >= 0;
        p->tem_video = d.video_idx >= 0;
        p->duracao = duracao;
        p->tocando = true;
        pthread_mutex_unlock(&p->lock);
    }
    wm_log("player: %s | audio=%s video=%s duracao=%.1fs", p->arquivo,
           d.audio_idx >= 0 ? "sim" : "nao", d.video_idx >= 0 ? "sim" : "nao",
           p->duracao);

    usou_audio = d.audio_idx >= 0 && p->audio && audio_ok(p->audio);
    inicio = wm_now();

    while (!p->pediu_parar) {
        /* Pausa: segura o thread sem consumir pacotes (o áudio já está
         * pausado no ALSA pelo player_toggle_pause). */
        if (p->pausado) {
            struct timespec ts = { 0, 20 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            inicio = wm_now() - (p->posicao > 0 ? p->posicao : 0);
            continue;
        }
        if (p->pediu_seek) {
            int64_t alvo;
            p->pediu_seek = false;
            alvo = (int64_t) (p->seek_para * AV_TIME_BASE);
            if (av_seek_frame(d.fmt, -1, alvo, AVSEEK_FLAG_BACKWARD) < 0) {
                /* Alguns containers exigem o stream específico. */
                if (d.audio_idx >= 0) {
                    av_seek_frame(d.fmt, d.audio_idx,
                                  (int64_t) (p->seek_para / av_q2d(d.fmt->streams[d.audio_idx]->time_base)),
                                  AVSEEK_FLAG_BACKWARD);
                }
            }
            if (d.audio_ctx) {
                avcodec_flush_buffers(d.audio_ctx);
            }
            if (d.video_ctx) {
                avcodec_flush_buffers(d.video_ctx);
            }
            if (p->audio) {
                audio_flush(p->audio);
            }
            pthread_mutex_lock(&p->lock);
            p->quadro_valido = false;
            p->terminou = false;
            pthread_mutex_unlock(&p->lock);
            inicio = wm_now() - p->seek_para;
            p->base_posicao = p->seek_para;
            continue;
        }

        {
            int rc = av_read_frame(d.fmt, d.pkt);
            if (rc < 0) {
                if (rc == AVERROR_EOF) {
                    break;
                }
                if (rc == AVERROR(EAGAIN)) {
                    struct timespec ts = { 0, 5 * 1000 * 1000 };
                    nanosleep(&ts, NULL);
                    continue;
                }
                wm_log("player: av_read_frame: %s", av_err2str(rc));
                break;
            }
        }

        if (d.pkt->stream_index == d.audio_idx && d.audio_ctx) {
            if (avcodec_send_packet(d.audio_ctx, d.pkt) >= 0) {
                while (avcodec_receive_frame(d.audio_ctx, d.frame) >= 0) {
                    escapar_e_converter(&d, p, d.frame);
                    if (!usou_audio) {
                        /* Sem placa de som (ou teste em PC): não há quem segure
                         * o ritmo, então o tempo do áudio é esperado aqui. */
                        double pts = (d.frame->pts != AV_NOPTS_VALUE)
                                         ? stream_segundos(d.fmt, d.audio_idx,
                                                           d.frame->pts)
                                         : 0.0;
                        double atraso = pts - (wm_now() - inicio);
                        if (atraso > 0.002 && atraso < 0.5) {
                            struct timespec ts;
                            ts.tv_sec = (time_t) atraso;
                            ts.tv_nsec = (long) ((atraso - (double) ts.tv_sec) * 1e9);
                            nanosleep(&ts, NULL);
                        }
                        p->posicao = pts;
                    }
                    av_frame_unref(d.frame);
                }
            }
        } else if (d.pkt->stream_index == d.video_idx && d.video_ctx) {
            if (avcodec_send_packet(d.video_ctx, d.pkt) >= 0) {
                while (avcodec_receive_frame(d.video_ctx, d.frame) >= 0) {
                    double pts = 0.0;
                    if (d.frame->pts != AV_NOPTS_VALUE) {
                        pts = stream_segundos(d.fmt, d.video_idx, d.frame->pts);
                    }
                    /* Ritmo: áudio manda quando existe. */
                    {
                        double agora = usou_audio
                                           ? (p->base_posicao + audio_clock(p->audio))
                                           : (wm_now() - inicio);
                        double atraso = pts - agora;
                        if (atraso > 0.005) {
                            if (atraso > 0.5) {
                                atraso = 0.5;
                            }
                            {
                                struct timespec ts;
                                ts.tv_sec = (time_t) atraso;
                                ts.tv_nsec = (long) ((atraso - (double) ts.tv_sec) * 1e9);
                                nanosleep(&ts, NULL);
                            }
                        }
                        if (atraso < -ATRASO_MAXIMO) {
                            /* Muito atrasado: descarta o quadro. */
                            av_frame_unref(d.frame);
                            continue;
                        }
                    }
                    preparar_quadro(&d, p, d.frame);
                    if (!usou_audio) {
                        p->posicao = pts;
                    }
                    av_frame_unref(d.frame);
                }
            }
        }
        av_packet_unref(d.pkt);
    }

    /* Drena o que ficou nos decodificadores (importante no fim do arquivo). */
    if (d.audio_ctx && !p->pediu_parar) {
        avcodec_send_packet(d.audio_ctx, NULL);
        while (avcodec_receive_frame(d.audio_ctx, d.frame) >= 0) {
            escapar_e_converter(&d, p, d.frame);
            av_frame_unref(d.frame);
        }
    }

    av_packet_free(&d.pkt);
    av_frame_free(&d.frame);
    sws_freeContext(d.sws);
    swr_free(&d.swr);
    if (d.audio_ctx) {
        avcodec_free_context(&d.audio_ctx);
    }
    if (d.video_ctx) {
        avcodec_free_context(&d.video_ctx);
    }
    avformat_close_input(&d.fmt);

    pthread_mutex_lock(&p->lock);
    p->tocando = false;
    p->terminou = !p->pediu_parar && !p->falhou;
    if (p->duracao > 0) {
        p->posicao = p->duracao;
    }
    pthread_mutex_unlock(&p->lock);
    wm_log("player: terminou (%s)", p->terminou ? "fim do arquivo" : "interrompido");
    return NULL;
}

Player *player_open(const char *arquivo, AudioOut *audio_out, int video_max_w,
                    int video_max_h, int volume)
{
    Player *p = calloc(1, sizeof(*p));
    AVFormatContext *fmt = NULL;

    if (!p || !arquivo) {
        free(p);
        return NULL;
    }
    snprintf(p->arquivo, sizeof(p->arquivo), "%.*s", (int) sizeof(p->arquivo) - 1,
             arquivo);
    p->audio = audio_out;
    p->volume = volume > 0 ? volume : 70;
    p->quadro_ativo = 0;
    p->video_max_w = video_max_w;
    p->video_max_h = video_max_h;
    pthread_mutex_init(&p->lock, NULL);

    /* Metadados e duração vêm de uma abertura rápida, antes do thread, para a
     * UI já mostrar título/duração enquanto o primeiro quadro não chega. */
    if (avformat_open_input(&fmt, arquivo, NULL, NULL) >= 0) {
        if (avformat_find_stream_info(fmt, NULL) >= 0) {
            AVDictionary *tags = fmt->metadata;
            AVDictionaryEntry *e;
            int idx_audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
            int idx_video = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

            if (idx_audio >= 0) {
                AVDictionary *st = fmt->streams[idx_audio]->metadata;
                e = av_dict_get(st, "title", NULL, AV_DICT_IGNORE_SUFFIX);
                if (!e) {
                    e = av_dict_get(tags, "title", NULL, AV_DICT_IGNORE_SUFFIX);
                }
                if (e && e->value) {
                    snprintf(p->titulo, sizeof(p->titulo), "%s", e->value);
                }
                copiar_tag(st, "artist", p->artista, sizeof(p->artista));
                if (!p->artista[0]) {
                    copiar_tag(tags, "artist", p->artista, sizeof(p->artista));
                }
                copiar_tag(st, "album", p->album, sizeof(p->album));
                if (!p->album[0]) {
                    copiar_tag(tags, "album", p->album, sizeof(p->album));
                }
            }
            if (!p->titulo[0]) {
                copiar_tag(tags, "title", p->titulo, sizeof(p->titulo));
            }
            if (!p->titulo[0]) {
                char artista[192] = "";
                char titulo[192] = "";
                titulo_pelo_nome(arquivo, titulo, sizeof(titulo), artista, sizeof(artista));
                snprintf(p->titulo, sizeof(p->titulo), "%s", titulo);
                if (!p->artista[0]) {
                    snprintf(p->artista, sizeof(p->artista), "%s", artista);
                }
            }
            if (fmt->duration > 0) {
                p->duracao = (double) fmt->duration / (double) AV_TIME_BASE;
            } else if (idx_audio >= 0) {
                p->duracao = (double) fmt->streams[idx_audio]->duration
                    * av_q2d(fmt->streams[idx_audio]->time_base);
            }
            if (idx_video >= 0) {
                p->largura = fmt->streams[idx_video]->codecpar->width;
                p->altura = fmt->streams[idx_video]->codecpar->height;
                p->tem_video = !(fmt->streams[idx_video]->disposition
                                 & AV_DISPOSITION_ATTACHED_PIC);
            }
            p->tem_audio = idx_audio >= 0;
        }
        avformat_close_input(&fmt);
    }

    if (pthread_create(&p->thread, NULL, thread_player, p) != 0) {
        wm_log("!!! player: nao criei o thread de decodificacao");
        pthread_mutex_destroy(&p->lock);
        free(p);
        return NULL;
    }
    p->thread_ok = true;
    return p;
}

void player_close(Player *p)
{
    if (!p) {
        return;
    }
    p->pediu_parar = true;
    if (p->thread_ok) {
        pthread_join(p->thread, NULL);
        p->thread_ok = false;
    }
    free(p->quadros[0]);
    free(p->quadros[1]);
    pthread_mutex_destroy(&p->lock);
    free(p);
}

void player_status(Player *p, struct PlayerStatus *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!p) {
        return;
    }
    pthread_mutex_lock(&p->lock);
    out->aberto = p->aberto;
    out->tocando = p->tocando;
    out->pausado = p->pausado;
    out->terminou = p->terminou;
    out->tem_video = p->tem_video;
    out->tem_audio = p->tem_audio;
    out->duracao = p->duracao;
    out->posicao = p->posicao;
    out->largura = p->largura;
    out->altura = p->altura;
    snprintf(out->titulo, sizeof(out->titulo), "%s", p->titulo);
    snprintf(out->artista, sizeof(out->artista), "%s", p->artista);
    snprintf(out->album, sizeof(out->album), "%s", p->album);
    snprintf(out->erro, sizeof(out->erro), "%s", p->erro);
    pthread_mutex_unlock(&p->lock);
}

void player_toggle_pause(Player *p)
{
    if (!p) {
        return;
    }
    p->pausado = !p->pausado;
    if (p->audio) {
        audio_pause(p->audio, p->pausado);
    }
    pthread_mutex_lock(&p->lock);
    p->posicao = p->pausado ? p->posicao : p->posicao;
    pthread_mutex_unlock(&p->lock);
}

void player_seek(Player *p, double segundos)
{
    if (!p) {
        return;
    }
    if (segundos < 0) {
        segundos = 0;
    }
    p->seek_para = segundos;
    p->pediu_seek = true;
}

void player_set_volume(Player *p, int volume)
{
    if (!p) {
        return;
    }
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
    p->volume = volume;
}

bool player_copy_video(Player *p, uint32_t *dst, int dst_w, int dst_h)
{
    bool copiou = false;

    if (!p || !dst) {
        return false;
    }
    pthread_mutex_lock(&p->lock);
    if (p->quadro_valido && p->quadros[p->quadro_ativo] && p->quadro_w == dst_w
        && p->quadro_h == dst_h) {
        memcpy(dst, p->quadros[p->quadro_ativo],
               (size_t) dst_w * (size_t) dst_h * sizeof(uint32_t));
        copiou = true;
    }
    pthread_mutex_unlock(&p->lock);
    return copiou;
}

bool player_video_dirty(Player *p)
{
    bool novo;

    if (!p) {
        return false;
    }
    pthread_mutex_lock(&p->lock);
    novo = p->quadro_novo;
    p->quadro_novo = false;
    pthread_mutex_unlock(&p->lock);
    return novo;
}
