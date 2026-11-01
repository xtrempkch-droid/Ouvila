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

int wm_utf8_encode(uint32_t cp, char *out)
{
    if (!out) {
        return 0;
    }
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp < 0x110000) {
        out[0] = (char) (0xF0 | (cp >> 18));
        out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char) (0x80 | (cp & 0x3F));
        return 4;
    }
    out[0] = '?';
    return 1;
}

/* Corta texto UTF-8 sem partir um caractere no meio: um byte solto de
 * sequência multi-byte vira "?" na fonte (ou pior, um glifo errado). */
size_t wm_utf8_copy(char *dst, size_t size, const char *src, size_t max_bytes)
{
    size_t usado = 0;
    size_t limite = size ? size - 1 : 0;

    if (!dst || size == 0) {
        return 0;
    }
    if (max_bytes && max_bytes < limite) {
        limite = max_bytes;
    }
    while (src && *src && usado < limite) {
        uint32_t cp;
        int n = wm_utf8_decode(src, &cp);

        if (n <= 0) {
            break;
        }
        if (usado + (size_t) n > limite) {
            break;
        }
        memcpy(dst + usado, src, (size_t) n);
        usado += (size_t) n;
        src += n;
    }
    dst[usado] = '\0';
    return usado;
}

/* ------------------------------------------------------------- ajustes --- */

/* O app escreve os próprios ajustes aqui. Não usamos o walkmam.conf porque
 * aquele arquivo é editado à mão no cartão (e é FAT: o CRLF voltaria).
 *
 * Formato: as linhas do arquivo ficam apontadas em g_linhas e as chaves
 * alteradas durante a sessão vivem em g_over (já como "chave=valor"). Gravar
 * junta os dois, pulando as linhas do arquivo que foram sobrepostas. Assim uma
 * sessão inteira de ajustes grava o cartão uma vez só, no fim. */
#define CONF_MAX_LINHAS 192

static char g_conf[WM_PATH_MAX];
static char g_conf_dados[4096];
static char *g_linhas[CONF_MAX_LINHAS];
static int g_n_linhas;
static char *g_over[CONF_MAX_LINHAS];
static int g_n_over;
static bool g_conf_carregado;

const char *wm_settings_path(void)
{
    static const char *const CANDIDATOS[] = {
        "/boot/walkmam-media.conf",
        "/storage/.config/walkmam-media.conf",
        "/tmp/walkmam-media.conf",
        NULL
    };

    if (g_conf[0]) {
        return g_conf;
    }
    for (int i = 0; CANDIDATOS[i]; i++) {
        char dir[WM_PATH_MAX];
        const char *barra;

        snprintf(dir, sizeof(dir), "%s", CANDIDATOS[i]);
        barra = strrchr(dir, '/');
        if (barra) {
            dir[barra - dir] = '\0';
            if (access(dir, W_OK) != 0) {
                continue;
            }
        }
        snprintf(g_conf, sizeof(g_conf), "%s", CANDIDATOS[i]);
        return g_conf;
    }
    snprintf(g_conf, sizeof(g_conf), "/tmp/walkmam-media.conf");
    return g_conf;
}

static void conf_limpar(void)
{
    for (int i = 0; i < g_n_over; i++) {
        free(g_over[i]);
    }
    g_n_over = 0;
    g_n_linhas = 0;
    g_conf_dados[0] = '\0';
}

bool wm_settings_load(void)
{
    const char *caminho = wm_settings_path();
    FILE *f;

    conf_limpar();
    g_conf_carregado = true;
    f = fopen(caminho, "r");
    if (!f) {
        return false;
    }
    size_t n = fread(g_conf_dados, 1, sizeof(g_conf_dados) - 1, f);
    fclose(f);
    g_conf_dados[n] = '\0';

    /* Quebra em linhas no lugar: cada '\n' vira '\0' e o ponteiro da próxima
     * linha é guardado. Assim cada valor já é uma string terminada. */
    {
        char *p = g_conf_dados;

        while (p && *p && g_n_linhas < CONF_MAX_LINHAS) {
            char *nl = strpbrk(p, "\r\n");
            char *ini_linha = p;

            if (nl) {
                *nl = '\0';
                p = nl + 1;
                /* CRLF: o '\r' já virou '\0' antes do '\n'. */
                if (nl[1] == '\n') {
                    p = nl + 2;
                }
            } else {
                p = NULL;
            }
            wm_trim(ini_linha);
            if (!ini_linha[0] || ini_linha[0] == '#') {
                continue;
            }
            if (!strchr(ini_linha, '=')) {
                continue;
            }
            g_linhas[g_n_linhas++] = ini_linha;
        }
    }
    return true;
}

/* Compara "chave=" sem exigir que a linha seja só a chave. */
static bool linha_da_chave(const char *linha, const char *chave)
{
    size_t n = strlen(chave);

    return strncmp(linha, chave, n) == 0 && linha[n] == '=';
}

const char *wm_settings_get(const char *chave, const char *padrao)
{
    if (!chave || !*chave) {
        return padrao;
    }
    if (!g_conf_carregado) {
        wm_settings_load();
    }
    for (int i = g_n_over - 1; i >= 0; i--) {
        if (linha_da_chave(g_over[i], chave)) {
            return g_over[i] + strlen(chave) + 1;
        }
    }
    for (int i = 0; i < g_n_linhas; i++) {
        if (linha_da_chave(g_linhas[i], chave)) {
            return g_linhas[i] + strlen(chave) + 1;
        }
    }
    return padrao;
}

int wm_settings_get_int(const char *chave, int padrao)
{
    const char *v = wm_settings_get(chave, NULL);

    if (!v || !*v) {
        return padrao;
    }
    return (int) strtol(v, NULL, 10);
}

void wm_settings_set(const char *chave, const char *valor)
{
    char linha[256];

    if (!chave || !*chave) {
        return;
    }
    if (!g_conf_carregado) {
        wm_settings_load();
    }
    snprintf(linha, sizeof(linha), "%s=%s", chave, valor ? valor : "");
    for (int i = 0; i < g_n_over; i++) {
        if (linha_da_chave(g_over[i], chave)) {
            char *novo = strdup(linha);

            if (novo) {
                free(g_over[i]);
                g_over[i] = novo;
            }
            return;
        }
    }
    if (g_n_over >= CONF_MAX_LINHAS) {
        return;
    }
    g_over[g_n_over] = strdup(linha);
    if (g_over[g_n_over]) {
        g_n_over++;
    }
}

void wm_settings_set_int(const char *chave, int valor)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%d", valor);
    wm_settings_set(chave, buf);
}

bool wm_settings_save(void)
{
    const char *caminho = wm_settings_path();
    FILE *f = fopen(caminho, "w");

    if (!f) {
        wm_log("!!! ajustes: nao consegui gravar %s", caminho);
        return false;
    }
    /* Cabeçalho só na primeira gravação, para o arquivo se explicar no PC. */
    if (!g_n_linhas && !g_n_over) {
        fprintf(f, "# ajustes do walkmam-media (escrito pelo app)\n");
    }
    for (int i = 0; i < g_n_linhas; i++) {
        bool sobreposta = false;

        for (int j = 0; j < g_n_over && !sobreposta; j++) {
            const char *eq = strchr(g_over[j], '=');
            size_t n = eq ? (size_t) (eq - g_over[j]) : 0;

            sobreposta = n > 0 && strncmp(g_linhas[i], g_over[j], n) == 0
                         && g_linhas[i][n] == '=';
        }
        if (!sobreposta) {
            fprintf(f, "%s\n", g_linhas[i]);
        }
    }
    for (int i = 0; i < g_n_over; i++) {
        fprintf(f, "%s\n", g_over[i]);
    }
    if (fclose(f) != 0) {
        return false;
    }
    /* O cartão é FAT e pode ser removido a quente: força para o disco. */
    sync();
    return true;
}

/* --------------------------------------------------------------- relógio --- */

bool wm_clock_set(const struct tm *novo)
{
    struct timespec ts;
    struct tm ajustado = *novo;

    if (!novo) {
        return false;
    }
    /* mktime normaliza campos fora de faixa (mês 13 -> janeiro do ano seguinte)
     * e respeita o fuso do /etc/localtime. */
    ajustado.tm_isdst = -1;
    ts.tv_sec = mktime(&ajustado);
    ts.tv_nsec = 0;
    if (ts.tv_sec == (time_t) -1) {
        return false;
    }
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        wm_log("!!! relogio: clock_settime falhou: %s (precisa de CAP_SYS_TIME)",
               strerror(errno));
        return false;
    }
    wm_log("relogio: ajustado para %04d-%02d-%02d %02d:%02d:%02d",
           ajustado.tm_year + 1900, ajustado.tm_mon + 1, ajustado.tm_mday,
           ajustado.tm_hour, ajustado.tm_min, ajustado.tm_sec);
    return true;
}
