/* input.c — gamepad e teclado lidos direto do evdev.
 *
 * Os handhelds usam o driver `rocknix-joypad` (17 botões + 4 eixos por SARADC)
 * com os códigos confirmados em docs/hardware-e-drivers.md §3.2.1:
 *   BTN_EAST  = botão A (direita)   BTN_SOUTH = B (baixo)
 *   BTN_NORTH = X (cima)            BTN_WEST  = Y (esquerda)
 *   BTN_TL/TR = L/R                 BTN_TL2/TR2 = L2/R2
 *   BTN_SELECT/START/MODE, BTN_THUMBL/THUMBR, BTN_DPAD_*
 * e o analógico esquerdo em ABS_X/ABS_Y.
 *
 * Não há biblioteca de joystick em uso (nem SDL nem libinput): o formato de
 * struct input_event do Linux é estável e é o mesmo que o kernel entrega.
 *
 * Um teclado USB é aceito ao mesmo tempo (é como se digita uma senha de Wi-Fi
 * ou se depura o aparelho), e o analógico esquerdo funciona como auto-repeat
 * do D-pad: segurar para baixo anda pela lista.
 */

#include "wm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DISPOSITIVOS_MAX 8
#define REPETICAO_MS 180
#define REPETICAO_ESPERA_MS 420

typedef struct {
    int fd;
    char caminho[64];
    char nome[128];
    bool eh_gamepad;
} Fonte;

struct Input {
    Fonte fontes[DISPOSITIVOS_MAX];
    int n_fontes;

    char nome_gamepad[128];
    char nome_teclado[128];

    int eixo_x;          /* -1, 0, 1 */
    int eixo_y;
    double proxima_repeticao;
    bool repetindo;
    int chave_repetindo;
};

static bool casar_nome(const char *nome, const char *chave)
{
    char baixo[128];
    size_t i;

    snprintf(baixo, sizeof(baixo), "%s", nome);
    for (i = 0; baixo[i]; i++) {
        if (baixo[i] >= 'A' && baixo[i] <= 'Z') {
            baixo[i] = (char) (baixo[i] - 'A' + 'a');
        }
    }
    return strstr(baixo, chave) != NULL;
}

static void ler_nome(const char *caminho, char *out, size_t size)
{
    char sysfs[WM_PATH_MAX];
    const char *base = strrchr(caminho, '/');

    out[0] = '\0';
    base = base ? base + 1 : caminho;
    snprintf(sysfs, sizeof(sysfs), "/sys/class/input/%s/device/name", base);
    if (!wm_read_file(sysfs, out, size)) {
        return;
    }
    wm_trim(out);
}

static void abrir_fontes(Input *in)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;

    if (!d) {
        wm_log("input: /dev/input nao existe");
        return;
    }
    while ((e = readdir(d)) && in->n_fontes < DISPOSITIVOS_MAX) {
        char caminho[64];
        char nome[128];
        int fd;
        unsigned long bits[8];

        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        snprintf(caminho, sizeof(caminho), "/dev/input/%.32s", e->d_name);
        fd = open(caminho, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        memset(bits, 0, sizeof(bits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
            close(fd);
            continue;
        }
        ler_nome(caminho, nome, sizeof(nome));
        if (!nome[0]) {
            close(fd);
            continue;
        }
        {
            Fonte *f = &in->fontes[in->n_fontes];
            bool gamepad = casar_nome(nome, "joypad") || casar_nome(nome, "retrogame")
                || casar_nome(nome, "gamepad") || casar_nome(nome, "rocknix");

            f->fd = fd;
            snprintf(f->caminho, sizeof(f->caminho), "%s", caminho);
            snprintf(f->nome, sizeof(f->nome), "%s", nome);
            f->eh_gamepad = gamepad;
            in->n_fontes++;
            if (gamepad && !in->nome_gamepad[0]) {
                snprintf(in->nome_gamepad, sizeof(in->nome_gamepad), "%s", nome);
            } else if (!gamepad && !in->nome_teclado[0]) {
                snprintf(in->nome_teclado, sizeof(in->nome_teclado), "%s", nome);
            }
            wm_log("input: %s (%s) -> %s", nome, caminho,
                   gamepad ? "gamepad" : "teclado/outro");
        }
    }
    closedir(d);
}

Input *input_open(void)
{
    Input *in = calloc(1, sizeof(*in));

    if (!in) {
        return NULL;
    }
    snprintf(in->nome_gamepad, sizeof(in->nome_gamepad), "(nenhum)");
    snprintf(in->nome_teclado, sizeof(in->nome_teclado), "(nenhum)");
    abrir_fontes(in);
    return in;
}

void input_close(Input *in)
{
    if (!in) {
        return;
    }
    for (int i = 0; i < in->n_fontes; i++) {
        close(in->fontes[i].fd);
    }
    free(in);
}

const char *input_gamepad_name(const Input *in)
{
    return in ? in->nome_gamepad : "(nenhum)";
}

const char *input_keyboard_name(const Input *in)
{
    return in ? in->nome_teclado : "(nenhum)";
}

/* Traduz o código do kernel para a ação lógica. Devolve WM_KEY_NENHUMA quando
 * o código não interessa. */
static int traduzir(unsigned short code, bool *digital)
{
    *digital = false;
    switch (code) {
    case BTN_EAST:          /* A físico (direita) */
    case BTN_START:
        return WM_KEY_OK;
    case BTN_SOUTH:         /* B físico (baixo) */
        return WM_KEY_VOLTAR;
    case BTN_NORTH:         /* X (cima) */
        return WM_KEY_TOCAR_PAUSAR;
    case BTN_WEST:          /* Y (esquerda) */
        return WM_KEY_MENU;
    case BTN_TL:
        return WM_KEY_ABA_ANT;
    case BTN_TR:
        return WM_KEY_ABA_PROX;
    case BTN_TL2:
        return WM_KEY_VOL_MENOS;
    case BTN_TR2:
        return WM_KEY_VOL_MAIS;
    case BTN_SELECT:
        return WM_KEY_PG_BAIXO;
    case BTN_MODE:
        return WM_KEY_PG_CIMA;
    case BTN_THUMBL:
        return WM_KEY_APAGAR;
    case BTN_THUMBR:
        return WM_KEY_MENU;
    case BTN_DPAD_UP:
        return WM_KEY_CIMA;
    case BTN_DPAD_DOWN:
        return WM_KEY_BAIXO;
    case BTN_DPAD_LEFT:
        return WM_KEY_ESQ;
    case BTN_DPAD_RIGHT:
        return WM_KEY_DIR;

    /* teclado USB */
    case KEY_UP:
        *digital = true;
        return WM_KEY_CIMA;
    case KEY_DOWN:
        *digital = true;
        return WM_KEY_BAIXO;
    case KEY_LEFT:
        *digital = true;
        return WM_KEY_ESQ;
    case KEY_RIGHT:
        *digital = true;
        return WM_KEY_DIR;
    case KEY_ENTER:
    case KEY_KPENTER:
        return WM_KEY_ENTER;
    case KEY_ESC:
        return WM_KEY_VOLTAR;
    case KEY_BACKSPACE:
        return WM_KEY_APAGAR;
    case KEY_SPACE:
        return WM_KEY_TOCAR_PAUSAR;
    case KEY_TAB:
        return WM_KEY_ABA_PROX;
    case KEY_PAGEDOWN:
        return WM_KEY_PG_BAIXO;
    case KEY_PAGEUP:
        return WM_KEY_PG_CIMA;
    case KEY_VOLUMEUP:
        return WM_KEY_VOL_MAIS;
    case KEY_VOLUMEDOWN:
        return WM_KEY_VOL_MENOS;
    case KEY_F1:
        return WM_KEY_MENU;
    case KEY_POWER:
        return WM_KEY_SAIR;
    default:
        return WM_KEY_NENHUMA;
    }
}

int input_poll(Input *in, struct KeyEvent *out, int max, int timeout_ms)
{
    int n = 0;
    double agora = wm_now();

    if (!in || !out || max <= 0) {
        return 0;
    }

    for (int i = 0; i < in->n_fontes && n < max; i++) {
        struct input_event ev;
        while (read(in->fontes[i].fd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_Y) {
                    /* Eixo do SARADC: 0..1023, centro ~512. A zona morta evita
                     * "andar sozinho" com o analógico solto. */
                    int valor = ev.value - 512;
                    int dir = valor > 200 ? 1 : (valor < -200 ? -1 : 0);

                    if (ev.code == ABS_X) {
                        in->eixo_x = dir;
                    } else {
                        in->eixo_y = dir;
                    }
                    if (dir != 0 && !in->repetindo) {
                        in->repetindo = true;
                        in->chave_repetindo = (ev.code == ABS_X)
                                                  ? (dir > 0 ? WM_KEY_DIR : WM_KEY_ESQ)
                                                  : (dir > 0 ? WM_KEY_BAIXO : WM_KEY_CIMA);
                        in->proxima_repeticao = agora + REPETICAO_ESPERA_MS / 1000.0;
                        out[n].key = in->chave_repetindo;
                        out[n].soltou = false;
                        out[n].usb = false;
                        n++;
                    } else if (dir == 0 && in->eixo_x == 0 && in->eixo_y == 0) {
                        in->repetindo = false;
                    }
                }
                continue;
            }
            if (ev.type != EV_KEY) {
                continue;
            }
            {
                bool digital = false;
                int acao = traduzir(ev.code, &digital);

                if (acao == WM_KEY_NENHUMA) {
                    continue;
                }
                /* value 2 = auto-repeat do teclado; ignora (temos o nosso). */
                if (ev.value == 2) {
                    continue;
                }
                if (n < max) {
                    out[n].key = acao;
                    out[n].soltou = (ev.value == 0);
                    out[n].usb = digital || !in->fontes[i].eh_gamepad;
                    n++;
                }
            }
        }
    }

    /* Auto-repeat do analógico (segurar para navegar). */
    if (n < max && in->repetindo && agora >= in->proxima_repeticao) {
        out[n].key = in->chave_repetindo;
        out[n].soltou = false;
        out[n].usb = false;
        n++;
        in->proxima_repeticao = agora + REPETICAO_MS / 1000.0;
    }

    if (n == 0 && timeout_ms > 0) {
        struct timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long) (timeout_ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        /* Segunda passada: um único evento pode ter chegado durante o sleep. */
        for (int i = 0; i < in->n_fontes && n < max; i++) {
            struct input_event ev;
            while (read(in->fontes[i].fd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {
                if (ev.type != EV_KEY || ev.value == 2) {
                    continue;
                }
                {
                    bool digital = false;
                    int acao = traduzir(ev.code, &digital);
                    if (acao == WM_KEY_NENHUMA) {
                        continue;
                    }
                    out[n].key = acao;
                    out[n].soltou = (ev.value == 0);
                    out[n].usb = digital || !in->fontes[i].eh_gamepad;
                    n++;
                }
            }
        }
    }
    return n;
}
