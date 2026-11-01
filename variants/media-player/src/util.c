/* util.c — log, configuração, textos e utilidades pequenas. */

#include "wm.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static FILE *g_log;

void wm_log_open(const char *path)
{
    if (!path || !*path) {
        return;
    }
    g_log = fopen(path, "a");
    if (g_log) {
        setvbuf(g_log, NULL, _IOLBF, 0);
    }
}

void wm_log(const char *fmt, ...)
{
    char linha[1024];
    time_t agora = time(NULL);
    struct tm tm;
    size_t usado;
    va_list ap;

    localtime_r(&agora, &tm);
    usado = strftime(linha, sizeof(linha), "%Y-%m-%d %H:%M:%S ", &tm);
    va_start(ap, fmt);
    vsnprintf(linha + usado, sizeof(linha) - usado, fmt, ap);
    va_end(ap);

    if (g_log) {
        fprintf(g_log, "%s\n", linha);
    }
    fprintf(stderr, "%s\n", linha);
}

void wm_log_close(void)
{
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

bool wm_read_file(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t n;

    if (!f) {
        return false;
    }
    n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

void wm_trim(char *s)
{
    char *ini = s;
    size_t len;

    if (!s) {
        return;
    }
    while (*ini && isspace((unsigned char) *ini)) {
        ini++;
    }
    if (ini != s) {
        memmove(s, ini, strlen(ini) + 1);
    }
    len = strlen(s);
    while (len > 0 && isspace((unsigned char) s[len - 1])) {
        s[--len] = '\0';
    }
}

/* Chave=valor, uma por linha. Aceita "#" como comentário e remove o CR do FAT. */
static bool conf_lookup(const char *path, const char *chave, char *out, size_t size)
{
    FILE *f;
    char linha[512];
    bool achou = false;

    if (!path || !*path) {
        return false;
    }
    f = fopen(path, "r");
    if (!f) {
        return false;
    }
    while (fgets(linha, sizeof(linha), f)) {
        char *igual;
        wm_trim(linha);
        if (!linha[0] || linha[0] == '#') {
            continue;
        }
        igual = strchr(linha, '=');
        if (!igual) {
            continue;
        }
        *igual = '\0';
        {
            char *k = linha;
            wm_trim(k);
            if (strcmp(k, chave) == 0) {
                char *v = igual + 1;
                wm_trim(v);
                snprintf(out, size, "%s", v);
                achou = true;
            }
        }
    }
    fclose(f);
    return achou;
}

const char *wm_transform_for_model(const char *requested)
{
    static char resultado[24];
    char modelo[128] = "";

    if (requested && *requested) {
        if (strcmp(requested, "auto") != 0) {
            snprintf(resultado, sizeof(resultado), "%s", requested);
            return resultado;
        }
    }
    wm_read_file("/proc/device-tree/model", modelo, sizeof(modelo));
    for (char *p = modelo; *p; p++) {
        *p = (char) tolower((unsigned char) *p);
    }
    /* A X55 tem painel 720x1280 montado deitado: precisa de rotate-90.
     * X35S/X35H têm painel 640x480 e ficam em normal. (Validado em hardware —
     * ver docs/hardware-e-drivers.md §3.1.) */
    if (strstr(modelo, "x55")) {
        snprintf(resultado, sizeof(resultado), "rotate-90");
    } else {
        snprintf(resultado, sizeof(resultado), "normal");
    }
    return resultado;
}

void wm_conf_read(WmConfig *cfg, const char *path)
{
    char valor[256];
    int n;

    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->conf_path, sizeof(cfg->conf_path), "%s", path ? path : "/boot/walkmam.conf");
    snprintf(cfg->audio_device, sizeof(cfg->audio_device), "default");
    cfg->initial_volume = 70;
    cfg->brightness = -1;
    cfg->exit_poweroff = true;
    cfg->log_to_file = true;
    snprintf(cfg->transform, sizeof(cfg->transform), "auto");
    snprintf(cfg->media_roots, sizeof(cfg->media_roots),
             "/storage/media:/boot/media:/storage/sdcard1/media:/storage/sdcard2/media");

    if (conf_lookup(cfg->conf_path, "display_transform", valor, sizeof(valor))) {
        snprintf(cfg->transform, sizeof(cfg->transform), "%.*s",
                 (int) sizeof(cfg->transform) - 1, valor);
    }
    if (conf_lookup(cfg->conf_path, "media_volume", valor, sizeof(valor))) {
        n = atoi(valor);
        if (n >= 0 && n <= 100) {
            cfg->initial_volume = n;
        }
    }
    if (conf_lookup(cfg->conf_path, "media_audio_out", valor, sizeof(valor))) {
        /* Aceita nome de dispositivo ALSA (default, hw:0,0, plug:...) ou
         * "-" para desligar o áudio (testes no PC). */
        snprintf(cfg->audio_device, sizeof(cfg->audio_device), "%.*s",
                 (int) sizeof(cfg->audio_device) - 1, valor);
    }
    if (conf_lookup(cfg->conf_path, "media_roots", valor, sizeof(valor))) {
        snprintf(cfg->media_roots, sizeof(cfg->media_roots), "%s", valor);
    }
    if (conf_lookup(cfg->conf_path, "media_exit", valor, sizeof(valor))) {
        cfg->exit_poweroff = strcmp(valor, "stay") != 0;
    }
    if (conf_lookup(cfg->conf_path, "media_brightness", valor, sizeof(valor))) {
        n = atoi(valor);
        if (n >= 1 && n <= 100) {
            cfg->brightness = n;
        }
    }
}

void wm_media_roots(const char *spec, char out[][WM_PATH_MAX], int *count, int max)
{
    const char *p = spec;
    int n = 0;

    *count = 0;
    if (!spec || !*spec) {
        return;
    }
    while (*p && n < max) {
        const char *fim = strchr(p, ':');
        size_t len = fim ? (size_t) (fim - p) : strlen(p);
        if (len > 0 && len < WM_PATH_MAX) {
            memcpy(out[n], p, len);
            out[n][len] = '\0';
            wm_trim(out[n]);
            if (out[n][0]) {
                n++;
            }
        }
        if (!fim) {
            break;
        }
        p = fim + 1;
    }
    *count = n;
}

double wm_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void wm_fmt_time(double segundos, char *out, size_t size)
{
    int total;

    if (!(segundos > 0) || segundos > 99 * 3600) {
        segundos = segundos > 0 ? segundos : 0;
    }
    total = (int) segundos;
    if (total >= 3600) {
        snprintf(out, size, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    } else {
        snprintf(out, size, "%d:%02d", total / 60, total % 60);
    }
}

void wm_fmt_bytes(double bytes, char *out, size_t size)
{
    static const char *unidades[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;

    if (bytes < 0) {
        bytes = 0;
    }
    while (bytes >= 1024.0 && i < 4) {
        bytes /= 1024.0;
        i++;
    }
    if (i == 0) {
        snprintf(out, size, "%.0f %s", bytes, unidades[i]);
    } else {
        snprintf(out, size, "%.1f %s", bytes, unidades[i]);
    }
}

void wm_mkdir_p(const char *path)
{
    char tmp[WM_PATH_MAX];
    size_t len;

    if (!path) {
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int wm_utf8_decode(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *) s;

    if (u[0] < 0x80) {
        *cp = u[0];
        return u[0] ? 1 : 0;
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t) (u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t) (u[0] & 0x0F) << 12) | ((uint32_t) (u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80
        && (u[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t) (u[0] & 0x07) << 18) | ((uint32_t) (u[1] & 0x3F) << 12)
            | ((uint32_t) (u[2] & 0x3F) << 6) | (u[3] & 0x3F);
        return 4;
    }
    *cp = '?';
    return 1;
}

/* Ordenação para listas: sem acento, sem caixa. */
int wm_casecmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char) tolower((unsigned char) *a);
        unsigned char cb = (unsigned char) tolower((unsigned char) *b);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a == *b) {
        return 0;
    }
    return *a ? 1 : -1;
}
