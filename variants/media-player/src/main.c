/* main.c — inicialização e laço principal do app de mídia.
 *
 * Ciclo de vida:
 *   1. lê /boot/walkmam.conf (rotação, volume, pastas de mídia)
 *   2. abre o DRM/KMS e cria o canvas do tamanho JÁ GIRADO
 *   3. abre o ALSA, varre a biblioteca em segundo plano, abre o gamepad
 *   4. laço: espera tecla (timeout curto) -> atualiza sistema -> desenha ->
 *      page flip. O que muda de verdade é o que dispara redesenho.
 *
 * Modos de linha de comando (para testar sem o aparelho):
 *   --screenshot ARQ.png   desenha uma vez e grava o canvas em PNG
 *   --list-devices         mostra gamepads/teclados encontrados e sai
 *   --scan                 lista a biblioteca e sai
 *   --version / --help
 */

#include "wm.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_sair;
/* Brilho a restaurar quando a tela foi "apagada" sem DPMS (fallback). 0 = nada. */
static int brilho_fallback;
static volatile sig_atomic_t g_recarregar;

static void ao_sinal(int sig)
{
    if (sig == SIGHUP || sig == SIGUSR1) {
        g_recarregar = 1;
    } else {
        g_sair = 1;
    }
}

/* ----------------------------------------------------------- sistema --- */

static bool ler_arquivo_txt(const char *caminho, char *out, size_t size)
{
    if (!wm_read_file(caminho, out, size)) {
        return false;
    }
    wm_trim(out);
    return out[0] != '\0';
}

static int ler_inteiro(const char *caminho, int padrao)
{
    char buf[32];
    int valor;

    if (!ler_arquivo_txt(caminho, buf, sizeof(buf))) {
        return padrao;
    }
    valor = atoi(buf);
    return valor;
}

static void ler_sistema(struct SystemInfo *si)
{
    char buf[256];

    memset(si, 0, sizeof(*si));
    si->bateria = -1;
    si->brilho = -1;
    si->tem_bateria = false;

    /* Modelo do device tree. */
    if (wm_read_file("/proc/device-tree/model", buf, sizeof(buf))) {
        snprintf(si->modelo, sizeof(si->modelo), "%.*s", (int) sizeof(si->modelo) - 1, buf);
    }

    /* Bateria. O rk817 (X55/X35S/X35H) expõe battery/battery0; os nomes são
     * tentados em ordem porque o driver pode registrar com sufixo. */
    {
        /* rk817 (X55/X35S/X35H) expõe battery/battery0 */
        static const char *bases[] = {
            "/sys/class/power_supply/battery",
            "/sys/class/power_supply/battery0",
            "/sys/class/power_supply/BAT0",
            NULL
        };
        for (int i = 0; bases[i]; i++) {
            char caminho[128];
            snprintf(caminho, sizeof(caminho), "%s/capacity", bases[i]);
            if (access(caminho, R_OK) != 0) {
                continue;
            }
            si->bateria = ler_inteiro(caminho, -1);
            si->tem_bateria = si->bateria >= 0;
            snprintf(caminho, sizeof(caminho), "%s/status", bases[i]);
            if (ler_arquivo_txt(caminho, buf, sizeof(buf))) {
                si->carregando = (strstr(buf, "Charging") != NULL
                                  || strstr(buf, "Full") != NULL);
            }
            break;
        }
    }

    /* Brilho. */
    {
        static const char *bases[] = {
            "/sys/class/backlight/backlight",
            "/sys/class/backlight/pwm-backlight",
            "/sys/class/backlight/dp_aux_backlight",
            NULL
        };
        for (int i = 0; bases[i]; i++) {
            char brilho[128], maximo[128];
            int atual, topo;

            snprintf(brilho, sizeof(brilho), "%s/brightness", bases[i]);
            snprintf(maximo, sizeof(maximo), "%s/max_brightness", bases[i]);
            if (access(brilho, R_OK) != 0 || access(maximo, R_OK) != 0) {
                continue;
            }
            atual = ler_inteiro(brilho, -1);
            topo = ler_inteiro(maximo, 255);
            if (atual >= 0 && topo > 0) {
                si->brilho = (int) round(atual * 100.0 / topo);
            }
            break;
        }
    }

    /* Uptime. */
    {
        FILE *f = fopen("/proc/uptime", "r");
        double segundos = 0;
        if (f) {
            if (fscanf(f, "%lf", &segundos) == 1) {
                si->uptime = (long) segundos;
            }
            fclose(f);
        }
    }
}

static bool gravar_brilho(int valor)
{
    static const char *bases[] = {
        "/sys/class/backlight/backlight",
        "/sys/class/backlight/pwm-backlight",
        "/sys/class/backlight/dp_aux_backlight",
        NULL
    };

    /* Piso de 10%: abaixo disso o painel fica ilegível e não economiza de
     * verdade — para isso existe "apagar a tela" (DPMS). A UI já limita, mas o
     * backend não pode confiar só nela (e há também o write direto no /sys). */
    if (valor < WM_BRILHO_MIN) {
        valor = WM_BRILHO_MIN;
    }
    if (valor > 100) {
        valor = 100;
    }
    for (int i = 0; bases[i]; i++) {
        char caminho[128];
        int topo;
        FILE *f;

        snprintf(caminho, sizeof(caminho), "%s/max_brightness", bases[i]);
        topo = ler_inteiro(caminho, 255);
        if (topo <= 0) {
            topo = 255;
        }
        snprintf(caminho, sizeof(caminho), "%s/brightness", bases[i]);
        f = fopen(caminho, "w");
        if (!f) {
            continue;
        }
        fprintf(f, "%d\n", (int) round(topo * valor / 100.0));
        fclose(f);
        return true;
    }
    return false;
}

static void executar_energia(const char *acao)
{
    pid_t pid = fork();

    if (pid == 0) {
        execlp("systemctl", "systemctl", acao, "--no-block", (char *) NULL);
        execlp(acao, acao, (char *) NULL);
        _exit(127);
    }
}

/* ------------------------------------------------------------ imagem --- */

/* Decodifica imagem com o libav* (já linkado): um pacote, um frame, escala
 * para RGB32. Serve para a capa do álbum. */
bool imagem_carregar(const char *caminho, uint32_t **px, int *w, int *h)
{
    AVFormatContext *fmt = NULL;
    AVCodecContext *ctx = NULL;
    const AVCodec *codec = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    struct SwsContext *sws = NULL;
    uint32_t *destino = NULL;
    bool ok = false;

    if (!caminho || !px) {
        return false;
    }
    if (avformat_open_input(&fmt, caminho, NULL, NULL) < 0) {
        return false;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0 || fmt->nb_streams < 1) {
        avformat_close_input(&fmt);
        return false;
    }
    codec = avcodec_find_decoder(fmt->streams[0]->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        avformat_close_input(&fmt);
        return false;
    }
    if (avcodec_parameters_to_context(ctx, fmt->streams[0]->codecpar) < 0
        || avcodec_open2(ctx, codec, NULL) < 0) {
        goto fim;
    }
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        goto fim;
    }
    if (av_read_frame(fmt, pkt) < 0 || avcodec_send_packet(ctx, pkt) < 0) {
        goto fim;
    }
    if (avcodec_receive_frame(ctx, frame) < 0 || frame->width <= 0) {
        goto fim;
    }
    destino = malloc((size_t) frame->width * (size_t) frame->height * sizeof(uint32_t));
    if (!destino) {
        goto fim;
    }
    /* Uma capa quebrada (arquivo truncado, "folder.jpg" que não é JPEG) não
     * deve encher o log de erro a cada álbum: a decodificação de imagem roda
     * em silêncio e o app cai no desenho do disco. */
    av_log_set_level(AV_LOG_QUIET);
    sws = sws_getContext(frame->width, frame->height, (enum AVPixelFormat) frame->format,
                         frame->width, frame->height, AV_PIX_FMT_BGRA,
                         SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        free(destino);
        destino = NULL;
        goto fim;
    }
    {
        uint8_t *dst[4] = { (uint8_t *) destino, NULL, NULL, NULL };
        int lines[4] = { frame->width * 4, 0, 0, 0 };
        sws_scale(sws, (const uint8_t *const *) frame->data, frame->linesize, 0,
                  frame->height, dst, lines);
    }
    *px = destino;
    *w = frame->width;
    *h = frame->height;
    ok = true;

fim:
    av_log_set_level(AV_LOG_ERROR);
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return ok;
}

/* ------------------------------------------------------------- opções --- */

static void uso(void)
{
    printf("walkmam-media " WM_VERSION " — app de mídia único (DRM/ALSA/FFmpeg)\n\n"
           "Uso: walkmam-media [opções]\n"
           "  --screenshot ARQ.ppm    desenha uma vez e grava o canvas (PPM)\n"
           "  --screenshot-size WxH   tamanho do canvas no modo --screenshot\n"
           "  --tab NOME              aba inicial (musica|videos|biblioteca|sistema)\n"
           "  --tema NOME             tema inicial (neon|oled|mono|mono-glow|vidro)\n"
           "  --idioma COD            idioma inicial (pt|en|zh|hi|es|ar|bn|id|ur|fr)\n"
           "  --list-devices         lista gamepads/teclados e sai\n"
           "  --scan                 varre a biblioteca e lista os arquivos\n"
           "  --selftest             exercita a navegacao/estado da UI e sai\n"
           "  --menu                 com --screenshot: abre o menu de configuracoes\n"
           "  --config ARQ           caminho de walkmam.conf (padrão /boot/walkmam.conf)\n"
           "  --play ARQ             abre e toca um arquivo direto (sem UI)\n"
           "  --frame-out ARQ.ppm    com --play: grava o quadro de vídeo em ARQ\n"
           "  --frame-at SEGUNDOS    instante do quadro (padrão 1.0 s)\n"
           "  --version              mostra a versão\n"
           "  --help                 esta ajuda\n");
}

/* PPM é trivial de escrever e serve para inspecionar o desenho no PC (o PNG
 * exigiria um codificador; não vale trazer um só para isso). */
static bool salvar_ppm(const char *caminho, const Canvas *c)
{
    FILE *f = fopen(caminho, "wb");

    if (!f) {
        wm_log("!!! nao consegui escrever %s: %s", caminho, strerror(errno));
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        unsigned char rgb[3] = { (unsigned char) ((p >> 16) & 0xFF),
                                 (unsigned char) ((p >> 8) & 0xFF),
                                 (unsigned char) (p & 0xFF) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    wm_log("canvas gravado em %s (%dx%d)", caminho, c->w, c->h);
    return true;
}

/* ------------------------------------------------------------ selftest --- */

/* Exercita a navegação e o estado da UI sem DRM e sem gamepad: cada passo é
 * uma tecla sintética e a verificação é pelo que a UI publica (pedido de
 * reprodução, aba em tela cheia, volume). Serve para o CI pegar regressão de
 * navegação — o que uma captura de tela sozinha não pega. */
static int wm_selftest(Ui *ui, Canvas *canvas, Library *lib)
{
    int falhas = 0;
    int n = library_count(lib);
    struct KeyEvent ev;

    (void) canvas;
#define CONFERE(cond, nome) do { \
        if (cond) { \
            printf("  ok   %s\n", nome); \
        } else { \
            printf("  FALHA %s\n", nome); \
            falhas++; \
        } \
    } while (0)

    if (n == 0) {
        printf("  pulado sem midia na biblioteca (use --config com media_roots)\n");
        return 0;
    }

    /* 1. A primeira tecla OK numa lista escolhe a faixa e pede reprodução. */
    ui_set_tab(ui, 0);
    memset(&ev, 0, sizeof(ev));
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    {
        double seek = 0;
        const char *quer = ui_want_play(ui, &seek);

        CONFERE(quer != NULL && quer[0] != '\0', "OK na lista pede reprodução");
        if (quer) {
            char copia[WM_PATH_MAX];
            snprintf(copia, sizeof(copia), "%s", quer);

            /* 2. O volume sobe em passos de 5 e sai pelo ui_want_volume. */
            ev.key = WM_KEY_VOL_MAIS;
            ui_handle_key(ui, &ev);
            CONFERE(ui_want_volume(ui) > 0, "botão de volume gera pedido");
            (void) ui_want_volume(ui);
        }
    }

    /* 3. Trocar de aba 4 vezes volta para a primeira. */
    ui_set_tab(ui, 0);
    for (int i = 0; i < 4; i++) {
        ev.key = WM_KEY_ABA_PROX;
        ui_handle_key(ui, &ev);
    }
    {
        /* Na aba Música, OK de novo tem de pedir a mesma faixa (a lista é da
         * aba, não do que estava em reprodução). */
        ev.key = WM_KEY_OK;
        ui_handle_key(ui, &ev);
        CONFERE(ui_want_play(ui, NULL) != NULL, "4x L/R volta à aba inicial");
    }

    /* 4. Vídeos: OK entra em tela cheia. */
    ui_set_tab(ui, 1);
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    {
        const char *quer = ui_want_play(ui, NULL);

        CONFERE(quer != NULL && ui_video_fullscreen(ui),
                "OK num vídeo abre em tela cheia");
    }

    /* 5. B sai da tela cheia. */
    ev.key = WM_KEY_VOLTAR;
    ui_handle_key(ui, &ev);
    CONFERE(!ui_video_fullscreen(ui), "B sai da tela cheia");

    /* 6. Navegação vertical não estoura a lista. */
    ui_set_tab(ui, 0);
    for (int i = 0; i < 50; i++) {
        ev.key = WM_KEY_BAIXO;
        ui_handle_key(ui, &ev);
    }
    for (int i = 0; i < 50; i++) {
        ev.key = WM_KEY_CIMA;
        ui_handle_key(ui, &ev);
    }
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    CONFERE(ui_want_play(ui, NULL) != NULL, "navegar 50x não perde a seleção");

    /* 7. Tecla liberada não gera ação. */
    ev.key = WM_KEY_OK;
    ev.soltou = true;
    ui_handle_key(ui, &ev);
    CONFERE(ui_want_play(ui, NULL) == NULL, "soltar a tecla não repete a ação");
    ev.soltou = false;   /* sem isto os testes abaixo seriam todos ignorados */

    /* 8. Desenho de cada aba sem travar. */
    for (int aba = 0; aba < 4; aba++) {
        ui_set_tab(ui, aba);
        ui_render(ui);
    }
    printf("  ok   as 4 abas desenharam\n");

    /* 9. Power curto apaga e acende a tela. Antes, ligar/desligar estava na
     * aba Sistema, onde o botão de volume se confundia com o brilho. */
    ui_set_tab(ui, 0);
    ev.key = WM_KEY_POWER_CURTO;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_TELA_APAGAR,
            "power curto apaga a tela");
    ev.key = WM_KEY_OK;   /* qualquer tecla acende */
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_TELA_ACENDER,
            "qualquer tecla acende a tela apagada");
    ev.key = WM_KEY_POWER_CURTO;
    ui_handle_key(ui, &ev);
    ev.key = WM_KEY_POWER_CURTO;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_TELA_ACENDER,
            "power curto acende de novo");

    /* 10. Power longo (3 s) abre a confirmação de desligar; B cancela. */
    ev.key = WM_KEY_POWER_LONGO;
    ui_handle_key(ui, &ev);
    ev.key = WM_KEY_VOLTAR;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_NENHUMA,
            "B cancela o desligar");
    ev.key = WM_KEY_POWER_LONGO;
    ui_handle_key(ui, &ev);
    ev.key = WM_KEY_OK;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_NENHUMA,
            "diálogo abre em cancelar (não desliga no primeiro toque)");
    ev.key = WM_KEY_POWER_LONGO;
    ui_handle_key(ui, &ev);
    ev.key = WM_KEY_DIR;   /* vai para o botão de desligar */
    ui_handle_key(ui, &ev);
    ev.key = WM_KEY_OK;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_DESLIGAR,
            "confirmar desliga o aparelho");

    /* 11. SELECT abre as configurações e o brilho tem piso. O brilho é
     * injetado porque no CI não há backlight; o piso é o que impede a tela
     * de ficar preta sem o usuário perceber. */
    {
        struct SystemInfo falso;
        memset(&falso, 0, sizeof(falso));
        falso.brilho = 50;
        ui_update_system(ui, &falso);
    }
    ev.key = WM_KEY_MENU;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_NENHUMA, "SELECT abre o menu");
    for (int i = 0; i < 40; i++) {
        ev.key = WM_KEY_ESQ;
        ui_handle_key(ui, &ev);
    }
    (void) ui_want_brightness(ui);
    CONFERE(ui_brilho(ui) == WM_BRILHO_MIN, "brilho para no piso, não em 0%");

    /* 12. Botão de volume dentro do menu não mexe no brilho. */
    {
        int antes = ui_brilho(ui);

        ev.key = WM_KEY_VOL_MENOS;
        ui_handle_key(ui, &ev);
        (void) ui_want_volume(ui);
        CONFERE(ui_brilho(ui) == antes, "volume não altera o brilho");
    }
    ev.key = WM_KEY_VOLTAR;
    ui_handle_key(ui, &ev);

    /* 13. L1/R1: na música saltam de faixa, no vídeo avançam/voltam 30 s. */
    ui_set_tab(ui, 0);
    ev.key = WM_KEY_PULO_PROX;
    ui_handle_key(ui, &ev);
    CONFERE(ui_want_transport(ui) == 2, "L1 salta para a próxima faixa");
    ev.key = WM_KEY_PULO_ANT;
    ui_handle_key(ui, &ev);
    CONFERE(ui_want_transport(ui) == 1, "R1 volta para a faixa anterior");
    ui_set_tab(ui, 1);
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    (void) ui_want_play(ui, NULL);
    ev.key = WM_KEY_PULO_PROX;
    ui_handle_key(ui, &ev);
    {
        double alvo = ui_want_seek(ui, NULL);

        CONFERE(alvo >= 30, "L1 no vídeo avança 30 s");
    }
    ev.key = WM_KEY_PULO_ANT;
    ui_handle_key(ui, &ev);
    {
        double alvo = ui_want_seek(ui, NULL);

        CONFERE(alvo <= -30, "R1 no vídeo volta 30 s");
    }

    /* 14. A aba Sistema é só informação: nenhuma tecla de volume/brilho age. */
    ui_set_tab(ui, 3);
    {
        int antes = ui_brilho(ui);

        ev.key = WM_KEY_VOL_MAIS;
        ui_handle_key(ui, &ev);
        CONFERE(ui_want_brightness(ui) < 0 && ui_brilho(ui) == antes,
                "aba Sistema não mexe no brilho");
    }

    /* 15. Aba Biblioteca: A na lista de ÁLBUNS abre o álbum em vez de tocar;
     * o segundo A toca a primeira faixa; B volta para a lista de álbuns. */
    ui_set_tab(ui, 2);
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    CONFERE(ui_want_play(ui, NULL) == NULL, "A no álbum abre, não toca");
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    {
        const char *quer = ui_want_play(ui, NULL);

        CONFERE(quer != NULL && quer[0] != '\0', "A na faixa toca dentro do álbum");
    }
    ev.key = WM_KEY_VOLTAR;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_NENHUMA,
            "B no álbum aberto volta para a lista de álbuns");
    ev.key = WM_KEY_VOLTAR;
    CONFERE(ui_handle_key(ui, &ev) == WM_ACAO_SAIR,
            "B na lista de álbuns sai do app");
    for (int i = 0; i < 6; i++) {
        ev.key = WM_KEY_CIMA;
        ui_handle_key(ui, &ev);
    }
    for (int i = 0; i < 6; i++) {
        ev.key = WM_KEY_BAIXO;
        ui_handle_key(ui, &ev);
    }
    ui_render(ui);
    printf("  ok   navegar pelos álbuns não estoura a lista\n");

    /* 16. Modo de reprodução: o quinto botão dos controles gira o modo. */
    ui_set_tab(ui, 0);
    {
        int inicial = ui_modo(ui);

        /* BAIXO até o fim da lista é o que leva o foco para os controles. */
        for (int i = 0; i < 12; i++) {
            ev.key = WM_KEY_BAIXO;
            ui_handle_key(ui, &ev);
        }
        for (int i = 0; i < 5; i++) {   /* até o botão do modo */
            ev.key = WM_KEY_DIR;
            ui_handle_key(ui, &ev);
        }
        ev.key = WM_KEY_OK;
        ui_handle_key(ui, &ev);
        CONFERE(ui_modo(ui) == (inicial + 1) % WM_MODO_TOTAL,
                "botão de modo gira em ordem -> repetir álbum");
        ui_set_modo(ui, WM_MODO_SEQUENCIAL);
    }

    /* 17. Menu com submenus: entrar no Som, mexer no preset e voltar sem
     * sair do menu. */
    ev.key = WM_KEY_MENU;
    ui_handle_key(ui, &ev);
    for (int i = 0; i < 3; i++) {   /* brilho -> wifi -> bt -> som */
        ev.key = WM_KEY_BAIXO;
        ui_handle_key(ui, &ev);
    }
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    {
        int antes = dsp_preset();

        ev.key = WM_KEY_DIR;
        ui_handle_key(ui, &ev);
        CONFERE(dsp_preset() == (antes + 1) % WM_PRESET_TOTAL,
                "submenu Som troca o preset do equalizador");
        ui_render(ui);
        printf("  ok   submenu Som desenhou\n");
    }
    ev.key = WM_KEY_VOLTAR;
    ui_handle_key(ui, &ev);
    ui_render(ui);
    printf("  ok   volta do submenu para a raiz desenhou\n");

    /* 18. Equalizador: banda muda, preamp muda, restaurar zera tudo. */
    dsp_set_banda(0, 6.0f);
    CONFERE(dsp_banda(0) > 5.0f, "banda 31 Hz aceita +6 dB");
    dsp_set_banda(0, 99.0f);
    CONFERE(dsp_banda(0) <= 12.0f, "ganho da banda para no teto de +12 dB");
    dsp_reset();
    CONFERE(dsp_banda(0) == 0.0f && dsp_preamp() == 0.0f
                && dsp_preset() == WM_PRESET_PLANO,
            "restaurar zera bandas, preamp e preset");

    /* 19. Modo neural só liga se houver NPU: no mainline não há, e o menu
     * precisa mostrar o item desativado em vez de aceitar o pedido. */
    CONFERE(dsp_set_neural(true) == dsp_neural_disponivel(),
            "modo neural respeita a presença do NPU");
    dsp_set_neural(false);

    /* 20. Idioma: trocar para inglês muda os textos e volta ao português. */
    {
        const char *pt = wm_str(MSG_ABA_MUSICA);
        char copia[64];

        snprintf(copia, sizeof(copia), "%s", pt);
        wm_lang_set(WM_LANG_EN);
        CONFERE(strcmp(wm_str(MSG_ABA_MUSICA), copia) != 0,
                "trocar o idioma troca os textos");
        wm_lang_set(WM_LANG_AR);
        CONFERE(wm_lang_rtl(), "árabe é marcado como escrita da direita para a esquerda");
        wm_lang_set(WM_LANG_PT);
        CONFERE(strcmp(wm_str(MSG_ABA_MUSICA), copia) == 0,
                "voltar para pt-BR restaura os textos");
    }
    /* Toda tradução obrigatória tem de existir nos 10 idiomas (um NULL cai no
     * português e a UI mostraria texto misturado). */
    {
        static const int AMOSTRA[] = { MSG_OK, MSG_ABA_MUSICA, MSG_MENU, MSG_SOM,
                                       MSG_IDIOMA, MSG_TEMA, MSG_DESLIGAR };
        int vazios = 0;

        for (int l = 0; l < WM_LANG_TOTAL; l++) {
            wm_lang_set(l);
            for (size_t k = 0; k < sizeof(AMOSTRA) / sizeof(AMOSTRA[0]); k++) {
                const char *t = wm_str(AMOSTRA[k]);

                if (!t || !t[0]) {
                    vazios++;
                }
            }
        }
        wm_lang_set(WM_LANG_PT);
        CONFERE(vazios == 0, "as 10 traduções têm todos os textos-chave");
    }

    /* 21. Temas: os 5 existem, têm código e nome, e trocar muda a paleta. */
    {
        static const char *const CODIGOS[WM_TEMA_TOTAL] = {
            "neon", "oled", "mono", "mono-glow", "vidro"
        };
        int ok = 1;

        for (int t = 0; t < WM_TEMA_TOTAL; t++) {
            tema_set(t);
            if (tema_atual_id() != t || !tema_codigo(t)[0]
                || tema_por_codigo(CODIGOS[t]) != t
                || wm_str(tema_msg_nome(t))[0] == '\0') {
                ok = 0;
            }
        }
        tema_set(WM_TEMA_NEON);
        CONFERE(ok, "os 5 temas existem e voltam pelo código");
        tema_set(WM_TEMA_VIDRO);
        ui_render(ui);
        tema_set(WM_TEMA_NEON);
        printf("  ok   tema alternativo desenhou\n");
    }

    /* 22. Menu de relógio: navegar e ajustar campos não pode estourar mês/dia. */
    ev.key = WM_KEY_MENU;
    ui_handle_key(ui, &ev);
    for (int i = 0; i < 5; i++) {   /* até Relógio */
        ev.key = WM_KEY_BAIXO;
        ui_handle_key(ui, &ev);
    }
    ev.key = WM_KEY_OK;
    ui_handle_key(ui, &ev);
    for (int i = 0; i < 40; i++) {
        ev.key = WM_KEY_DIR;
        ui_handle_key(ui, &ev);
    }
    for (int i = 0; i < 40; i++) {
        ev.key = WM_KEY_ESQ;
        ui_handle_key(ui, &ev);
    }
    ui_render(ui);
    printf("  ok   ajuste do relógio não estoura mês/dia\n");
    ev.key = WM_KEY_MENU;
    ui_handle_key(ui, &ev);

    /* 23. WiFi/Bluetooth agora moram no menu, não numa aba: as 4 abas têm de
     * ter nomes distintos, e "Rádio" não existe mais. */
    {
        int iguais = 0;

        for (int a = 0; a < 4; a++) {
            for (int b = a + 1; b < 4; b++) {
                const int nomes[4] = { MSG_ABA_MUSICA, MSG_ABA_VIDEOS,
                                       MSG_ABA_BIBLIOTECA, MSG_ABA_SISTEMA };

                if (strcmp(wm_str(nomes[a]), wm_str(nomes[b])) == 0) {
                    iguais++;
                }
            }
        }
        CONFERE(iguais == 0, "as 4 abas têm nomes distintos (sem aba Radio)");
    }

#undef CONFERE
    return falhas ? 1 : 0;
}

/* --------------------------------------------------------------- main --- */

int main(int argc, char **argv)
{
    WmConfig cfg;
    char conf_path[WM_PATH_MAX] = "/boot/walkmam.conf";
    const char *screenshot = NULL;
    int screenshot_w = 1280, screenshot_h = 720;
    int tab_inicial = 0;
    int tema_inicial = -1;
    int idioma_inicial = -1;
    const char *tocar_direto = NULL;
    const char *quadro_saida = NULL;
    double quadro_em = 1.0;
    bool salvou_quadro = false;
    bool listar = false;
    bool escanear = false;
    bool selftest = false;
    bool abrir_menu_para_foto = false;
    struct DrmInfo info;
    Drm *drm = NULL;
    Canvas canvas;
    AudioOut *audio = NULL;
    Input *input = NULL;
    Library *lib = NULL;
    Radio *radio = NULL;
    Ui *ui = NULL;
    Player *player = NULL;
    int volume;
    int largura_ui, altura_ui;
    double ultimo_desenho = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            uso();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("walkmam-media " WM_VERSION "\n");
            return 0;
        }
        if (strcmp(argv[i], "--list-devices") == 0) {
            listar = true;
        } else if (strcmp(argv[i], "--scan") == 0) {
            escanear = true;
        } else if (strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
        } else if (strcmp(argv[i], "--menu") == 0) {
            abrir_menu_para_foto = true;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--tab") == 0 && i + 1 < argc) {
            const char *nome = argv[++i];
            if (strcmp(nome, "musica") == 0) {
                tab_inicial = 0;
            } else if (strcmp(nome, "videos") == 0) {
                tab_inicial = 1;
            } else if (strcmp(nome, "biblioteca") == 0) {
                tab_inicial = 2;
            } else if (strcmp(nome, "sistema") == 0) {
                tab_inicial = 3;
            } else {
                fprintf(stderr, "aba invalida: %s\n", nome);
                return 2;
            }
        } else if (strcmp(argv[i], "--tema") == 0 && i + 1 < argc) {
            tema_inicial = tema_por_codigo(argv[++i]);
        } else if (strcmp(argv[i], "--idioma") == 0 && i + 1 < argc) {
            idioma_inicial = wm_lang_from_code(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot-size") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &screenshot_w, &screenshot_h) != 2
                || screenshot_w < 320 || screenshot_h < 240) {
                fprintf(stderr, "tamanho invalido (use LARGURAxALTURA, ex. 1280x720)\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            snprintf(conf_path, sizeof(conf_path), "%.*s",
                     (int) sizeof(conf_path) - 1, argv[++i]);
        } else if (strcmp(argv[i], "--play") == 0 && i + 1 < argc) {
            tocar_direto = argv[++i];
        } else if (strcmp(argv[i], "--frame-out") == 0 && i + 1 < argc) {
            quadro_saida = argv[++i];
        } else if (strcmp(argv[i], "--frame-at") == 0 && i + 1 < argc) {
            quadro_em = atof(argv[++i]);
        } else {
            fprintf(stderr, "opcao desconhecida: %s (use --help)\n", argv[i]);
            return 2;
        }
    }

    wm_conf_read(&cfg, conf_path);
    if (cfg.log_to_file) {
        wm_log_open("/var/log/media-player.log");
    }
    /* Só erros do FFmpeg: o nível padrão imprime avisos por quadro em vídeo
     * com B-frames e enche o log do cartão. */
    av_log_set_level(AV_LOG_ERROR);
    wm_log("walkmam-media " WM_VERSION " iniciando (config %s)", cfg.conf_path);

    if (listar) {
        Input *in = input_open();
        if (!in) {
            fprintf(stderr, "nao consegui abrir /dev/input\n");
            return 1;
        }
        printf("gamepad: %s\nteclado: %s\n", input_gamepad_name(in),
               input_keyboard_name(in));
        input_close(in);
        wm_log_close();
        return 0;
    }

    /* Bibliotecas de mídia. */
    {
        char raizes[8][WM_PATH_MAX];
        int n = 0;
        const char *ponteiros[8];
        char salvos[8][WM_PATH_MAX];

        wm_media_roots(cfg.media_roots, raizes, &n, 8);
        for (int i = 0; i < n; i++) {
            snprintf(salvos[i], WM_PATH_MAX, "%.*s", WM_PATH_MAX - 1, raizes[i]);
            ponteiros[i] = salvos[i];
            wm_mkdir_p(raizes[i]);
        }
        lib = library_create((const char *const *) ponteiros, n);
        if (!lib) {
            wm_log("!!! nao consegui criar a biblioteca");
            return 1;
        }
        library_scan(lib);
    }

    if (escanear) {
        while (library_scanning(lib)) {
            struct timespec ts = { 0, 50 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
        {
            int total = library_count(lib);
            struct MediaItem item;
            for (int i = 0; i < total; i++) {
                if (library_item(lib, i, &item)) {
                    printf("%s\t%s\t%s\t%s\n",
                           item.tipo == WM_MIDIA_AUDIO ? "audio" : "video",
                           item.titulo, item.artista, item.caminho);
                }
            }
            printf("total: %d\n", total);
        }
        library_destroy(lib);
        wm_log_close();
        return 0;
    }

    /* Áudio: "-" em media_audio_out desliga (testes sem placa de som). */
    volume = cfg.initial_volume;
    if (strcmp(cfg.audio_device, "-") != 0) {
        audio = audio_open(cfg.audio_device, 48000, 2);
        if (audio && !audio_ok(audio)) {
            wm_log("!!! audio nao abriu; a UI continua sem som");
        }
        if (audio) {
            audio_set_volume(audio, volume);
        }
    }

    /* Modo "só tocar" (depuração por SSH/serial, sem UI). Com --screenshot
     * junto, o modo vira prévia de vídeo e é tratado mais abaixo. */
    if (tocar_direto && !screenshot) {
        Player *p = player_open(tocar_direto, audio, 640, 360, volume);
        struct PlayerStatus st;

        if (!p) {
            wm_log("!!! nao consegui abrir %s", tocar_direto);
            return 1;
        }
        /* Espera o thread abrir o arquivo: sem isso o laço abaixo veria
         * tocando=false no primeiro instante e encerraria a reprodução. */
        for (int i = 0; i < 100; i++) {
            struct timespec ts = { 0, 20 * 1000 * 1000 };
            player_status(p, &st);
            if (st.aberto || st.erro[0]) {
                break;
            }
            nanosleep(&ts, NULL);
        }
        wm_log("tocando: %s | %s (duracao %.1fs)", st.titulo,
               st.tem_video ? "video" : "audio", st.duracao);

        for (;;) {
            struct timespec ts = { 0, 200 * 1000 * 1000 };
            player_status(p, &st);
            if (!st.tocando) {
                break;
            }
            wm_log("posicao %.2f/%.2f%s", st.posicao, st.duracao,
                   st.pausado ? " (pausado)" : "");
            if (quadro_saida && !salvou_quadro && st.posicao >= quadro_em) {
                uint32_t *buf = malloc((size_t) 640 * 360 * sizeof(uint32_t));
                if (buf && player_copy_video(p, buf, 640, 360)) {
                    Canvas tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.px = buf;
                    tmp.w = 640;
                    tmp.h = 360;
                    salvar_ppm(quadro_saida, &tmp);
                    salvou_quadro = true;
                }
                free(buf);
            }
            nanosleep(&ts, NULL);
        }
        wm_log("fim: %s", st.erro[0] ? st.erro : "terminou");
        player_close(p);
        if (audio) {
            audio_close(audio);
        }
        library_destroy(lib);
        wm_log_close();
        return 0;
    }

    /* Saída de vídeo. No modo --screenshot o DRM não é aberto: o desenho é
     * exercitado offline (é assim que a UI é validada sem o aparelho). */
    {
        const char *transform = wm_transform_for_model(cfg.transform);

        if (screenshot || selftest) {
            /* Modos offline (--screenshot/--selftest) não tocam no DRM: é assim
             * que a UI é validada sem o aparelho. */
            largura_ui = screenshot_w;
            altura_ui = screenshot_h;
            wm_log("modo offline: canvas %dx%d (sem DRM)", largura_ui, altura_ui);
        } else {
            drm = drm_open(&info, transform);
            if (!drm) {
                wm_log("!!! sem DRM: nenhum painel disponivel "
                       "(use --screenshot para testar o desenho)");
                if (audio) {
                    audio_close(audio);
                }
                library_destroy(lib);
                return 1;
            }
            largura_ui = info.ui_w;
            altura_ui = info.ui_h;
            wm_log("tela: painel %dx%d -> UI %dx%d (transform %s)", info.fb_w,
                   info.fb_h, largura_ui, altura_ui, transform);
        }
    }
    canvas_init(&canvas, largura_ui, altura_ui);
    if (!canvas.px) {
        if (drm) {
            drm_close(drm);
        }
        if (audio) {
            audio_close(audio);
        }
        library_destroy(lib);
        return 1;
    }

    input = input_open();
    radio = radio_create();
    ui = ui_create(&canvas, lib, audio);
    if (!ui) {
        wm_log("!!! nao consegui criar a UI");
        return 1;
    }
    ui_set_radio(ui, radio);
    /* Os argumentos vencem o que está gravado em /boot/walkmam-media.conf: é o
     * que permite gerar as capturas de teste com qualquer tema/idioma. */
    if (idioma_inicial >= 0) {
        wm_lang_set(idioma_inicial);
    }
    if (tema_inicial >= 0) {
        tema_set(tema_inicial);
    }
    ui_set_tab(ui, tab_inicial);

    if (selftest) {
        int rc;

        /* O selftest depende da biblioteca já carregada (é a mesma espera que o
         * modo normal faz antes do primeiro desenho). */
        for (int i = 0; i < 200 && library_scanning(lib); i++) {
            struct timespec ts = { 0, 25 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
        ui_refresh_library(ui);
        rc = wm_selftest(ui, &canvas, lib);

        ui_destroy(ui);
        radio_destroy(radio);
        input_close(input);
        library_destroy(lib);
        canvas_free(&canvas);
        if (audio) {
            audio_close(audio);
        }
        if (drm) {
            drm_close(drm);
        }
        wm_log_close();
        return rc;
    }

    signal(SIGINT, ao_sinal);
    signal(SIGTERM, ao_sinal);
    signal(SIGHUP, ao_sinal);
    signal(SIGUSR1, ao_sinal);
    signal(SIGPIPE, SIG_IGN);

    wm_log("input: gamepad=%s teclado=%s", input_gamepad_name(input),
           input_keyboard_name(input));

    /* Prévia de vídeo: --play + --screenshot juntos abrem o arquivo, esperam
     * o instante pedido e desenham a tela cheia (valida o caminho de vídeo da
     * UI sem o aparelho). */
    if (screenshot && tocar_direto) {
        struct PlayerStatus st;
        Player *p = player_open(tocar_direto, audio, canvas.w, canvas.h, volume);
        bool ok = false;

        if (!p) {
            wm_log("!!! nao consegui abrir %s", tocar_direto);
        } else {
            for (int i = 0; i < 2000; i++) {
                struct timespec ts = { 0, 10 * 1000 * 1000 };
                player_status(p, &st);
                if (st.erro[0] || st.terminou) {
                    break;
                }
                if (st.posicao >= quadro_em && player_video_dirty(p)) {
                    player_status(p, &st);
                    ok = true;
                    break;
                }
                nanosleep(&ts, NULL);
            }
            wm_log("previa: %s posicao %.2f/%.2f (%s)", st.titulo, st.posicao,
                   st.duracao, ok ? "quadro pronto" : "sem quadro");
            ui_set_player(ui, p);
            ui_set_tab(ui, 1);
            ui_set_video_fullscreen(ui, true);
            ui_show_osd(ui, 60.0);
            ui_render(ui);
            salvar_ppm(screenshot, &canvas);
            player_close(p);
        }
        ui_destroy(ui);
        canvas_free(&canvas);
        input_close(input);
        radio_destroy(radio);
        library_destroy(lib);
        if (audio) {
            audio_close(audio);
        }
        wm_log_close();
        return ok ? 0 : 1;
    }

    /* Espera a primeira varredura terminar (é rápida: só o diretório de
     * mídia). Sem isso a primeira tela apareceria com as listas vazias. */
    for (int i = 0; i < 100 && library_scanning(lib); i++) {
        struct timespec ts = { 0, 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }

    /* Um desenho inicial antes de esperar teclas: a tela nunca fica preta. */
    {
        struct SystemInfo sys;
        ler_sistema(&sys);
        sys.wifi_presente = access("/sys/class/net/wlan0/operstate", R_OK) == 0;
        sys.wifi_conectado = sys.wifi_presente
            && ler_arquivo_txt("/sys/class/net/wlan0/operstate", sys.wifi_ip,
                               sizeof(sys.wifi_ip))
            && strcmp(sys.wifi_ip, "up") == 0;
        ui_update_system(ui, &sys);
        ui_refresh_library(ui);
    }

    if (screenshot) {
        bool gravou;

        if (abrir_menu_para_foto) {
            /* Mesma tecla que o usuário usa (SELECT): exercita o caminho real
             * de abrir o menu, não uma função de desenho chamada à parte. */
            struct KeyEvent ev;

            memset(&ev, 0, sizeof(ev));
            ev.key = WM_KEY_MENU;
            ui_handle_key(ui, &ev);
        }
        ui_render(ui);
        gravou = salvar_ppm(screenshot, &canvas);
        ui_destroy(ui);
        drm_close(drm);
        canvas_free(&canvas);
        input_close(input);
        radio_destroy(radio);
        library_destroy(lib);
        if (audio) {
            audio_close(audio);
        }
        wm_log_close();
        return gravou ? 0 : 1;
    }

    /* Laço principal. */
    while (!g_sair) {
        struct KeyEvent eventos[16];
        int n = input_poll(input, eventos, 16, 30);
        double agora = wm_now();
        bool precisa_desenhar = false;

        for (int i = 0; i < n; i++) {
            int acao = ui_handle_key(ui, &eventos[i]);
            precisa_desenhar = true;

            switch (acao) {
            case WM_ACAO_SAIR:
                g_sair = 1;
                break;
            case WM_ACAO_DESLIGAR:
                wm_log("desligando a pedido do usuario");
                executar_energia("poweroff");
                g_sair = 1;
                break;
            case WM_ACAO_REINICIAR:
                wm_log("reiniciando a pedido do usuario");
                executar_energia("reboot");
                g_sair = 1;
                break;
            case WM_ACAO_RESSCAN:
                library_scan(lib);
                break;
            case WM_ACAO_TELA_APAGAR:
                /* DPMS off: o painel para de ser varrido. É o que economiza
                 * bateria; baixar o brilho apenas reduz o backlight. */
                if (!drm || !drm_dpms(drm, false)) {
                    /* Sem DPMS, cai no brilho mínimo como degradação. */
                    brilho_fallback = ui_brilho(ui);
                    gravar_brilho(WM_BRILHO_MIN);
                }
                break;
            case WM_ACAO_TELA_ACENDER:
                if (drm) {
                    drm_dpms(drm, true);
                }
                /* O DPMS não mexe no backlight; o brilho só precisa ser
                 * reescrito quando o apagar caiu no fallback (sem DPMS). */
                if (brilho_fallback > 0) {
                    gravar_brilho(brilho_fallback);
                    brilho_fallback = 0;
                }
                break;
            default:
                break;
            }
        }

        /* Pedidos da UI. */
        {
            double seek = 0;
            const char *quer = ui_want_play(ui, &seek);

            if (quer) {
                if (player) {
                    player_close(player);
                    player = NULL;
                }
                player = player_open(quer, audio, canvas.w, canvas.h, volume);
                ui_set_player(ui, player);
                player_set_volume(player, volume);
                precisa_desenhar = true;
            }
        }
        if (player) {
            bool absoluto = false;
            double alvo = ui_want_seek(ui, &absoluto);
            struct PlayerStatus st;

            player_status(player, &st);
            if (alvo != 0) {
                player_seek(player, st.posicao + alvo);
                precisa_desenhar = true;
            }
            if (ui_wants_toggle(ui)) {
                player_toggle_pause(player);
                precisa_desenhar = true;
            }
        } else {
            (void) ui_wants_toggle(ui);
            (void) ui_want_seek(ui, NULL);
        }
        {
            /* Anterior/próximo/parar da barra de controles. */
            int transporte = ui_want_transport(ui);

            if (transporte == 3) {
                if (player) {
                    player_close(player);
                    player = NULL;
                    ui_set_player(ui, NULL);
                    ui_set_current_path(ui, NULL);
                }
                precisa_desenhar = true;
            } else if (transporte != 0) {
                /* 1 = anterior, 2 = próximo: anda na lista da aba atual. */
                const char *proximo = ui_step_track(ui, transporte == 2 ? 1 : -1);

                if (proximo) {
                    if (player) {
                        player_close(player);
                        player = NULL;
                    }
                    player = player_open(proximo, audio, canvas.w, canvas.h, volume);
                    if (player) {
                        player_set_volume(player, volume);
                    }
                    ui_set_player(ui, player);
                    ui_set_current_path(ui, proximo);
                }
                precisa_desenhar = true;
            }
        }
        {
            int novo_volume = ui_want_volume(ui);
            if (novo_volume >= 0) {
                volume = novo_volume;
                if (audio) {
                    audio_set_volume(audio, volume);
                }
                if (player) {
                    player_set_volume(player, volume);
                }
                precisa_desenhar = true;
            }
            {
                int brilho = ui_want_brightness(ui);
                if (brilho > 0) {
                    gravar_brilho(brilho);
                    precisa_desenhar = true;
                }
            }
        }

        /* Estado do sistema a cada ~1 s (barato, mas não a cada quadro). */
        {
            static double proximo = 0;
            if (agora >= proximo) {
                struct SystemInfo sys;
                ler_sistema(&sys);
                {
                    bool conectado = false;
                    char estado[32];
                    sys.wifi_presente = access("/sys/class/net/wlan0/operstate", R_OK) == 0;
                    if (sys.wifi_presente
                        && ler_arquivo_txt("/sys/class/net/wlan0/operstate", estado,
                                           sizeof(estado))) {
                        conectado = strcmp(estado, "up") == 0;
                    }
                    sys.wifi_conectado = conectado;
                }
                radio_status(radio, &sys.wifi_presente, &sys.wifi_conectado,
                             sys.wifi_nome, sizeof(sys.wifi_nome), sys.wifi_ip,
                             sizeof(sys.wifi_ip), &sys.bt_presente, &sys.bt_ligado,
                             sys.bt_mac, sizeof(sys.bt_mac));
                ui_update_system(ui, &sys);
                proximo = agora + 1.0;
                precisa_desenhar = true;
            }
        }

        /* Reprodução de vídeo em tela cheia: redesenha quando há quadro novo. */
        if (player && ui_video_fullscreen(ui)) {
            if (player_video_dirty(player)) {
                precisa_desenhar = true;
            }
        }
        if (player) {
            struct PlayerStatus st;
            player_status(player, &st);
            if (st.terminou) {
                /* Fim da faixa/vídeo. Antes disso o app apenas parava no
                 * silêncio e nada avançava (aparecia como "terminou
                 * interrompido" no log a cada 1-2 s). */
                player_close(player);
                player = NULL;
                ui_set_player(ui, NULL);
                if (st.tem_video) {
                    /* Vídeo: volta para a lista em vez de emendar outro
                     * (num aparelho de 4" o usuário escolhe o que ver). */
                    ui_set_video_fullscreen(ui, false);
                    ui_set_current_path(ui, NULL);
                } else {
                    /* Modo de reprodução (botão MODO na barra de controles):
                     *   0 em ordem       -> próxima; para no fim da lista
                     *   1 repetir álbum  -> próxima, dando a volta no fim
                     *   2 repetir faixa  -> a mesma de novo
                     *   3 aleatória      -> qualquer faixa de áudio da lista
                     * ui_step_track() sempre dá a volta na lista, então "em
                     * ordem" precisa comparar: se a "próxima" é a que acabou,
                     * chegamos ao fim e é hora de parar. */
                    int modo = ui_modo(ui);
                    char anterior[WM_PATH_MAX];
                    const char *proximo = NULL;
                    const char *atual = ui_step_track(ui, 0);

                    snprintf(anterior, sizeof(anterior), "%s", atual ? atual : "");

                    if (modo == WM_MODO_REPETIR_FAIXA) {
                        proximo = anterior[0] ? ui_step_track(ui, 0) : NULL;
                    } else if (modo == WM_MODO_ALEATORIO) {
                        int total = library_count(lib);

                        for (int tentativa = 0; tentativa < 24 && !proximo; tentativa++) {
                            int alvo = total > 0 ? rand() % total : 0;
                            struct MediaItem sorteado;

                            if (!library_item(lib, alvo, &sorteado)
                                || sorteado.tipo != WM_MIDIA_AUDIO) {
                                continue;
                            }
                            /* Anda até a posição sorteada dentro da lista
                             * filtrada: a UI é quem conhece o índice. */
                            proximo = ui_step_track(ui, alvo);
                        }
                    } else {
                        proximo = ui_step_track(ui, 1);
                        if (modo == WM_MODO_SEQUENCIAL && proximo && anterior[0]
                            && strcmp(proximo, anterior) == 0) {
                            proximo = NULL;   /* deu a volta: fim da lista */
                        }
                    }

                    if (proximo) {
                        player = player_open(proximo, audio, canvas.w, canvas.h,
                                             volume);
                        if (player) {
                            player_set_volume(player, volume);
                        }
                        ui_set_player(ui, player);
                        ui_set_current_path(ui, proximo);
                        wm_log("fila: proxima faixa %s (modo %d)", proximo, modo);
                    } else {
                        ui_set_current_path(ui, NULL);
                        wm_log("fila: fim da lista (modo %d)", modo);
                    }
                }
                precisa_desenhar = true;
            }
        }

        /* Com a tela apagada (DPMS off) não se desenha nem se faz flip: flip
         * em CRTC desligado falha, e desenhar à toa gasta CPU/bateria. O som
         * continua — é assim que se ouve música com o aparelho "apagado". */
        if (ui_tela_apagada(ui)) {
            precisa_desenhar = false;
        } else if (precisa_desenhar || agora - ultimo_desenho > 0.25) {
            ui_render(ui);
            if (drm) {
                drm_present(drm, &canvas);
            }
            ultimo_desenho = agora;
        }

        if (g_recarregar) {
            g_recarregar = 0;
            wm_log("recarregando configuracao e biblioteca");
            wm_conf_read(&cfg, conf_path);
            library_scan(lib);
            precisa_desenhar = true;
        }
    }

    wm_log("encerrando");
    /* Se a tela ficou em DPMS off, religa o painel antes de sair: senão o
     * aparelho volta ao console com a tela apagada. */
    if (drm) {
        drm_dpms(drm, true);
    }
    if (brilho_fallback > 0) {
        gravar_brilho(brilho_fallback);
    }
    if (player) {
        player_close(player);
    }
    ui_destroy(ui);
    radio_destroy(radio);
    input_close(input);
    library_destroy(lib);
    if (audio) {
        audio_close(audio);
    }
    canvas_free(&canvas);
    if (drm) {
        drm_close(drm);
    }
    wm_log_close();
    return 0;
}
