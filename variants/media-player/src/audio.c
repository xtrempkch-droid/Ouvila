/* audio.c — saída de som pelo ALSA, sem servidor de áudio.
 *
 * O ALSA aqui é o "relógio" da reprodução: audio_write() bloqueia enquanto o
 * buffer do hardware está cheio, então quem decodifica fica naturalmente no
 * ritmo certo (nada de timer artificial). Para o vídeo, audio_clock() dá a
 * posição real do áudio e as imagens são apresentadas contra ela.
 *
 * Formato fixo S16_LE entrelaçado: é o que o codec rk817 (X55/X35S/X35H)
 * aceita sem conversão no kernel, e a conversão de qualquer entrada para esse
 * formato é feita no decoder com libswresample.
 */

#include "wm.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <pthread.h>

struct AudioOut {
    snd_pcm_t *pcm;
    int rate;
    int channels;
    int volume;              /* 0..100 */
    bool pausado;
    bool falhou;
    char device[64];
    uint64_t frames_escritos;
    uint64_t frames_zerados; /* quando não há áudio, o tempo anda pelo mixer */
    int16_t *dsp_buf;        /* cópia de trabalho para o EQ (não mexe no decoder) */
    int dsp_cap;             /* capacidade de dsp_buf em amostras */
    pthread_mutex_t lock;
};

static int configurar(AudioOut *a)
{
    snd_pcm_hw_params_t *hw;
    unsigned int rate = (unsigned int) a->rate;
    int rc;

    snd_pcm_hw_params_alloca(&hw);
    if ((rc = snd_pcm_hw_params_any(a->pcm, hw)) < 0) {
        return rc;
    }
    if ((rc = snd_pcm_hw_params_set_access(a->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        return rc;
    }
    if ((rc = snd_pcm_hw_params_set_format(a->pcm, hw, SND_PCM_FORMAT_S16_LE)) < 0) {
        return rc;
    }
    if ((rc = snd_pcm_hw_params_set_channels(a->pcm, hw, (unsigned int) a->channels)) < 0) {
        return rc;
    }
    if ((rc = snd_pcm_hw_params_set_rate_near(a->pcm, hw, &rate, 0)) < 0) {
        return rc;
    }
    a->rate = (int) rate;
    if ((rc = snd_pcm_hw_params(a->pcm, hw)) < 0) {
        return rc;
    }
    /* 200 ms de buffer: espaço para o decodificador respirar sem estourar. */
    if ((rc = snd_pcm_set_params(a->pcm, SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 (unsigned int) a->channels, (unsigned int) a->rate,
                                 1, 200000)) < 0) {
        return rc;
    }
    snd_pcm_prepare(a->pcm);
    return 0;
}

AudioOut *audio_open(const char *device, int rate, int channels)
{
    AudioOut *a = calloc(1, sizeof(*a));
    int rc;

    if (!a) {
        return NULL;
    }
    a->rate = rate > 0 ? rate : 48000;
    a->channels = channels > 0 ? channels : 2;
    a->volume = 70;
    snprintf(a->device, sizeof(a->device), "%.*s", (int) sizeof(a->device) - 1,
             device ? device : "default");
    pthread_mutex_init(&a->lock, NULL);

    rc = snd_pcm_open(&a->pcm, a->device, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        wm_log("!!! audio: nao abriu '%s': %s", a->device, snd_strerror(rc));
        a->falhou = true;
        return a;
    }
    rc = configurar(a);
    if (rc < 0) {
        wm_log("!!! audio: nao configurou '%s': %s", a->device, snd_strerror(rc));
        snd_pcm_close(a->pcm);
        a->pcm = NULL;
        a->falhou = true;
        return a;
    }
    /* Avança o mixer em passos pequenos: no rk817 o controle "Playback Volume"
     * é o único caminho de volume por software no ALSA puro. */
    {
        snd_mixer_t *mixer = NULL;
        snd_mixer_elem_t *elem;
        if (snd_mixer_open(&mixer, 0) == 0 && mixer) {
            if (snd_mixer_attach(mixer, a->device) == 0
                && snd_mixer_selem_register(mixer, NULL, NULL) == 0
                && snd_mixer_load(mixer) == 0) {
                for (elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
                    if (snd_mixer_selem_has_playback_volume(elem)) {
                        long min = 0, max = 0;
                        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
                        /* Máximo no mixer; o volume efetivo é aplicado em
                         * software (ver audio_set_volume). */
                        snd_mixer_selem_set_playback_volume_all(elem, max);
                        wm_log("audio: mixer '%s' no maximo (volume por software em %d%%)",
                               snd_mixer_selem_get_name(elem), a->volume);
                        (void) min;
                    }
                }
            }
            snd_mixer_close(mixer);
        }
    }
    dsp_init(a->rate, a->channels);
    wm_log("audio: %s aberto a %d Hz, %d canais", a->device, a->rate, a->channels);
    return a;
}

void audio_close(AudioOut *a)
{
    if (!a) {
        return;
    }
    if (a->pcm) {
        snd_pcm_drain(a->pcm);
        snd_pcm_close(a->pcm);
    }
    free(a->dsp_buf);
    pthread_mutex_destroy(&a->lock);
    free(a);
}

int audio_write(AudioOut *a, const int16_t *dados, int frames)
{
    int feito = 0;

    if (!a || !a->pcm || frames <= 0) {
        return 0;
    }
    pthread_mutex_lock(&a->lock);
    while (a->pausado) {
        pthread_mutex_unlock(&a->lock);
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&a->lock);
    }
    pthread_mutex_unlock(&a->lock);

    /* O equalizador trabalha numa cópia: o buffer do decodificador pode estar
     * sendo lido por outra thread (o quadro de vídeo, por exemplo) e não é
     * nosso para alterar. */
    if (a->dsp_cap < frames) {
        int16_t *novo_buf = realloc(a->dsp_buf, (size_t) frames * (size_t) a->channels
                                                 * sizeof(int16_t));
        if (novo_buf) {
            a->dsp_buf = novo_buf;
            a->dsp_cap = frames;
        }
    }
    if (a->dsp_buf && a->dsp_cap >= frames) {
        memcpy(a->dsp_buf, dados, (size_t) frames * (size_t) a->channels * sizeof(int16_t));
        dsp_processar(a->dsp_buf, frames);
        dados = a->dsp_buf;
    }

    while (feito < frames) {
        snd_pcm_sframes_t escritos = snd_pcm_writei(
            a->pcm, dados + (size_t) feito * (size_t) a->channels,
            (snd_pcm_uframes_t) (frames - feito));
        if (escritos < 0) {
            if (escritos == -EAGAIN) {
                snd_pcm_wait(a->pcm, 100);
                continue;
            }
            /* Underrun/EPIPE: recupera e continua de onde parou. */
            if (snd_pcm_recover(a->pcm, (int) escritos, 1) != 0) {
                wm_log("!!! audio: erro irrecuperavel: %s", snd_strerror((int) escritos));
                a->falhou = true;
                break;
            }
            continue;
        }
        feito += (int) escritos;
        a->frames_escritos += (uint64_t) escritos;
    }
    return feito;
}

void audio_set_volume(AudioOut *a, int volume_0_100)
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem;

    if (!a) {
        return;
    }
    if (volume_0_100 < 0) {
        volume_0_100 = 0;
    }
    if (volume_0_100 > 100) {
        volume_0_100 = 100;
    }
    pthread_mutex_lock(&a->lock);
    a->volume = volume_0_100;
    pthread_mutex_unlock(&a->lock);

    if (!a->pcm) {
        return;
    }
    if (snd_mixer_open(&mixer, 0) != 0 || !mixer) {
        return;
    }
    /* O mixer vai para o MÁXIMO e o volume é aplicado em software (no
     * decodificador). Antes ele também era escalado aqui: em 50% o resultado
     * era 25% (50% do mixer vezes 50% do software). Além disso, depender do
     * nome do controle ("Playback Volume") é frágil: no rk817 o mux do
     * alto-falante já precisa ficar em HP. */
    if (snd_mixer_attach(mixer, a->device) == 0
        && snd_mixer_selem_register(mixer, NULL, NULL) == 0
        && snd_mixer_load(mixer) == 0) {
        for (elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
            if (snd_mixer_selem_has_playback_volume(elem)) {
                long min = 0, max = 0;
                snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
                snd_mixer_selem_set_playback_volume_all(elem, max);
                (void) min;
            }
        }
    }
    snd_mixer_close(mixer);
}

int audio_volume(AudioOut *a)
{
    int v;

    if (!a) {
        return 0;
    }
    pthread_mutex_lock(&a->lock);
    v = a->volume;
    pthread_mutex_unlock(&a->lock);
    return v;
}

void audio_pause(AudioOut *a, bool pausado)
{
    if (!a) {
        return;
    }
    pthread_mutex_lock(&a->lock);
    a->pausado = pausado;
    pthread_mutex_unlock(&a->lock);
    if (a->pcm) {
        if (pausado) {
            snd_pcm_pause(a->pcm, 1);
        } else {
            snd_pcm_pause(a->pcm, 0);
        }
    }
}

void audio_flush(AudioOut *a)
{
    if (!a || !a->pcm) {
        return;
    }
    snd_pcm_drop(a->pcm);
    snd_pcm_prepare(a->pcm);
    /* Zera o relógio: quem chama (seek) reposiciona a partir do ponto novo.
     * Sem isso audio_clock() continuaria somando o que já foi tocado antes do
     * seek e o vídeo seria apresentado no ritmo errado. */
    pthread_mutex_lock(&a->lock);
    a->frames_escritos = 0;
    pthread_mutex_unlock(&a->lock);
}

double audio_clock(AudioOut *a)
{
    snd_pcm_sframes_t atraso = 0;
    uint64_t tocados;

    if (!a || !a->pcm) {
        return 0.0;
    }
    if (snd_pcm_delay(a->pcm, &atraso) != 0 || atraso < 0) {
        atraso = 0;
    }
    tocados = a->frames_escritos > (uint64_t) atraso
                  ? a->frames_escritos - (uint64_t) atraso
                  : 0;
    return (double) tocados / (double) a->rate;
}

bool audio_ok(const AudioOut *a)
{
    return a && a->pcm && !a->falhou;
}

void audio_stats(const AudioOut *a, struct AudioStats *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!a) {
        return;
    }
    out->open = a->pcm != NULL;
    out->rate = a->rate;
    out->channels = a->channels;
    out->frames_written = a->frames_escritos;
    snprintf(out->device, sizeof(out->device), "%s", a->device);
}
