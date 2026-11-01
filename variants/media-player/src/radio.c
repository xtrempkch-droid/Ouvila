/* radio.c — Wi-Fi e Bluetooth do aparelho.
 *
 * Estado lido direto do kernel/sysfs (operstate, /sys/class/bluetooth) e as
 * ações delegadas ao NetworkManager (nmcli) e ao BlueZ (bluetoothctl), que já
 * são os serviços da imagem — não há aqui nenhuma camada nova: é o mesmo
 * caminho que a imagem usa desde o Wi-Fi/BT do RTL8821CS.
 *
 * Tudo assíncrono do ponto de vista da UI: as chamadas podem levar segundos
 * (scan de Wi-Fi, pareamento) e travar o desenho seria pior que não ter o
 * recurso. Por isso a UI só pergunta "já terminou?".
 */

#include "wm.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define REDES_MAX 32
#define BT_DEV_MAX 16

struct Radio {
    pthread_mutex_t lock;
    pthread_t thread;
    pthread_t poller;
    bool poller_ativo;
    bool ocupado;

    /* Estado em cache, atualizado por um thread a cada POLL_SEGUNDOS. A UI lê
     * daqui: chamar o nmcli no caminho do desenho travaria a interface (cada
     * chamada leva dezenas de ms). */
    bool wifi_presente;
    bool wifi_conectado;
    char ssid[64];
    char ip[32];
    bool bt_presente;
    bool bt_ligado;
    char bt_mac[32];
    char ultimo_erro[128];
    char ultimo_aviso[128];

    struct Rede redes[REDES_MAX];
    int n_redes;

    struct BtDevice bt_devs[BT_DEV_MAX];
    int n_bt_devs;
};

/* Roda um comando e captura stdout. Sem shell: argumentos fixos, nada de
 * interpolar texto que veio da rede (SSID de terceiro não vira comando). */
static int executar(const char *const argv[], char *saida, size_t tamanho)
{
    int pipefd[2];
    pid_t pid;
    size_t usado = 0;
    int status = 0;

    if (saida && tamanho) {
        saida[0] = '\0';
    }
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], (char *const *) argv);
        _exit(127);
    }
    close(pipefd[1]);
    while (saida && usado + 1 < tamanho) {
        ssize_t n = read(pipefd[0], saida + usado, tamanho - 1 - usado);
        if (n <= 0) {
            break;
        }
        usado += (size_t) n;
    }
    if (saida) {
        saida[usado] = '\0';
    }
    close(pipefd[0]);
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool comando_existe(const char *nome)
{
    const char *caminhos[] = { "/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/" };

    for (size_t i = 0; i < sizeof(caminhos) / sizeof(caminhos[0]); i++) {
        char caminho[128];
        snprintf(caminho, sizeof(caminho), "%s%s", caminhos[i], nome);
        if (access(caminho, X_OK) == 0) {
            return true;
        }
    }
    return false;
}

#define POLL_SEGUNDOS 5

/* Leitura barata do kernel (sysfs), sem processos. */
static bool ler_sysfs(char *ssid, size_t ssid_size, char *ip, size_t ip_size,
                      bool *wifi_presente, bool *wifi_conectado, bool *bt_presente)
{
    char valor[64];

    if (wifi_presente) {
        *wifi_presente = access("/sys/class/net/wlan0/operstate", R_OK) == 0;
    }
    if (wifi_conectado) {
        *wifi_conectado = wm_read_file("/sys/class/net/wlan0/operstate", valor,
                                       sizeof(valor))
            && strncmp(valor, "up", 2) == 0;
    }
    if (bt_presente) {
        *bt_presente = access("/sys/class/bluetooth", R_OK) == 0;
    }
    /* Sem os comandos: o Wi-Fi conectado só mostra o IP via nmcli, que é feito
     * no thread de polling. Aqui não tocamos em SSID/IP. */
    (void) ssid;
    (void) ssid_size;
    (void) ip;
    (void) ip_size;
    return true;
}

/* Atualização "cara" (nmcli/bluetoothctl), fora do caminho do desenho. */
static void atualizar_estado_caro(Radio *r)
{
    char saida[1024];
    char ssid[64] = "";
    char ip[32] = "";
    bool bt_ligado = false;
    char mac[32] = "";
    struct BtDevice devs[BT_DEV_MAX];
    int n_devs = 0;
    const char *const argv_devices[] = { "bluetoothctl", "devices", NULL };

    memset(devs, 0, sizeof(devs));

    if (comando_existe("nmcli")) {
        const char *argv[] = { "nmcli", "-t", "-f",
                               "GENERAL.CONNECTION,IP4.ADDRESS",
                               "device", "show", "wlan0", NULL };
        if (executar(argv, saida, sizeof(saida)) == 0) {
            char *p = strstr(saida, "GENERAL.CONNECTION:");
            if (p) {
                snprintf(ssid, sizeof(ssid), "%.*s", (int) sizeof(ssid) - 1,
                         p + strlen("GENERAL.CONNECTION:"));
                wm_trim(ssid);
            }
            p = strstr(saida, "IP4.ADDRESS");
            if (p) {
                char *dois = strchr(p, ':');
                if (dois) {
                    snprintf(ip, sizeof(ip), "%.*s", (int) sizeof(ip) - 1, dois + 1);
                    wm_trim(ip);
                    {
                        char *barra = strchr(ip, '/');
                        if (barra) {
                            *barra = '\0';
                        }
                    }
                }
            }
        }
    }
    if (comando_existe("bluetoothctl")) {
        const char *argv[] = { "bluetoothctl", "show", NULL };
        if (executar(argv, saida, sizeof(saida)) == 0) {
            char *p = strstr(saida, "Powered:");
            if (p && strstr(p, "yes")) {
                bt_ligado = true;
            }
            p = strstr(saida, "Controller ");
            if (p) {
                snprintf(mac, sizeof(mac), "%.17s", p + strlen("Controller "));
                wm_trim(mac);
            }
        }
        /* Lista de dispositivos: o menu precisa de MAC + nome para conectar.
         * Formato de `bluetoothctl devices`: "Device AA:BB:CC:DD:EE:FF nome".
         * O nome pode ter espaço, então não dá para usar sscanf com %s. */
        if (executar(argv_devices, saida, sizeof(saida)) == 0) {
            char *linha = saida;

            while (linha && *linha && n_devs < BT_DEV_MAX) {
                char *nl = strchr(linha, '\n');
                if (nl) {
                    *nl = '\0';
                }
                if (strncmp(linha, "Device ", 7) == 0 && strlen(linha) >= 24) {
                    struct BtDevice *d = &devs[n_devs++];

                    memset(d, 0, sizeof(*d));
                    {
                        char copia_mac[20];
                        char copia_nome[80];

                        snprintf(copia_mac, sizeof(copia_mac), "%.19s", linha + 7);
                        wm_trim(copia_mac);
                        snprintf(d->mac, sizeof(d->mac), "%s", copia_mac);
                        copia_nome[0] = '\0';
                        if (strlen(linha) > 25) {
                            snprintf(copia_nome, sizeof(copia_nome), "%.70s", linha + 25);
                            wm_trim(copia_nome);
                        }
                        snprintf(d->nome, sizeof(d->nome), "%s",
                                 copia_nome[0] ? copia_nome : d->mac);
                    }
                }
                linha = nl ? nl + 1 : NULL;
            }
        }
        /* Quem está conectado/pareado agora. */
        for (int i = 0; i < n_devs; i++) {
            char info[512];
            const char *argv_info[] = { "bluetoothctl", "info", devs[i].mac, NULL };

            if (executar(argv_info, info, sizeof(info)) != 0) {
                continue;
            }
            if (strstr(info, "Connected: yes")) {
                devs[i].conectado = true;
            }
            if (strstr(info, "Paired: yes")) {
                devs[i].pareado = true;
            }
        }
    }

    pthread_mutex_lock(&r->lock);
    snprintf(r->ssid, sizeof(r->ssid), "%s", ssid);
    snprintf(r->ip, sizeof(r->ip), "%s", ip);
    r->bt_ligado = bt_ligado;
    snprintf(r->bt_mac, sizeof(r->bt_mac), "%s", mac);
    memcpy(r->bt_devs, devs, sizeof(devs));
    r->n_bt_devs = n_devs;
    pthread_mutex_unlock(&r->lock);
}

static void *thread_poll(void *arg)
{
    Radio *r = arg;

    while (r->poller_ativo) {
        atualizar_estado_caro(r);
        /* Espera em pedaços para o destroy não depender de um sleep de 5 s. */
        for (int i = 0; i < POLL_SEGUNDOS * 10 && r->poller_ativo; i++) {
            struct timespec ts = { 0, 100 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

Radio *radio_create(void)
{
    Radio *r = calloc(1, sizeof(*r));

    if (!r) {
        return NULL;
    }
    pthread_mutex_init(&r->lock, NULL);
    /* SIGCHLD fica no padrão: waitpid() em executar() recolhe cada filho (com
     * SIG_IGN o waitpid falharia e os processos ficariam como zumbis). */
    r->poller_ativo = true;
    if (pthread_create(&r->poller, NULL, thread_poll, r) != 0) {
        r->poller_ativo = false;
        wm_log("!!! radio: sem thread de polling (o estado fica so no sysfs)");
    }
    return r;
}

void radio_destroy(Radio *r)
{
    if (!r) {
        return;
    }
    r->poller_ativo = false;
    pthread_join(r->poller, NULL);
    pthread_mutex_lock(&r->lock);
    if (r->ocupado) {
        pthread_mutex_unlock(&r->lock);
        pthread_join(r->thread, NULL);
    } else {
        pthread_mutex_unlock(&r->lock);
    }
    pthread_mutex_destroy(&r->lock);
    free(r);
}

void radio_status(Radio *r, bool *wifi_presente, bool *wifi_conectado,
                  char *ssid, size_t ssid_size, char *ip, size_t ip_size,
                  bool *bt_presente, bool *bt_ligado, char *bt_mac,
                  size_t bt_mac_size)
{
    bool presente = false, conectado = false, bt = false;

    ler_sysfs(NULL, 0, NULL, 0, &presente, &conectado, &bt);

    if (wifi_presente) {
        *wifi_presente = presente;
    }
    if (wifi_conectado) {
        *wifi_conectado = conectado;
    }
    if (bt_presente) {
        *bt_presente = bt;
    }
    if (!r) {
        if (ssid && ssid_size) {
            ssid[0] = '\0';
        }
        if (ip && ip_size) {
            ip[0] = '\0';
        }
        if (bt_ligado) {
            *bt_ligado = false;
        }
        if (bt_mac && bt_mac_size) {
            bt_mac[0] = '\0';
        }
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (ssid && ssid_size) {
        snprintf(ssid, ssid_size, "%s", r->ssid);
    }
    if (ip && ip_size) {
        snprintf(ip, ip_size, "%s", r->ip);
    }
    if (bt_ligado) {
        *bt_ligado = r->bt_ligado;
    }
    if (bt_mac && bt_mac_size) {
        snprintf(bt_mac, bt_mac_size, "%s", r->bt_mac);
    }
    pthread_mutex_unlock(&r->lock);
}

/* ---------------------------------------------------------------- scan --- */

static void *thread_scan_wifi(void *arg)
{
    Radio *r = arg;
    char saida[8192];
    const char *argv[] = { "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                           "device", "wifi", "list", "--rescan", "yes", NULL };
    int rc = executar(argv, saida, sizeof(saida));

    pthread_mutex_lock(&r->lock);
    r->n_redes = 0;
    if (rc != 0 || !saida[0]) {
        snprintf(r->ultimo_erro, sizeof(r->ultimo_erro),
                 "nmcli nao listou redes (o radio esta presente?)");
    } else {
        char *linha = strtok(saida, "\n");
        while (linha && r->n_redes < REDES_MAX) {
            /* Formato: IN-USE:SSID:SIGNAL:SECURITY. O SSID pode conter ':'
             * escapado como "\:", então os campos são separados do fim. */
            char *campos[4];
            int achados = 0;
            char *fim = linha + strlen(linha);

            campos[3] = NULL;
            for (int i = 2; i >= 0 && achados < 3; i--) {
                char *dois = fim;
                while (dois > linha) {
                    dois--;
                    if (*dois == ':' && (dois == linha || dois[-1] != '\\')) {
                        campos[i + 1] = dois + 1;
                        *dois = '\0';
                        fim = dois;
                        achados++;
                        break;
                    }
                }
            }
            campos[0] = linha;
            if (campos[1] && campos[2] && campos[3] && campos[1][0]) {
                struct Rede *rede = &r->redes[r->n_redes];
                snprintf(rede->ssid, sizeof(rede->ssid), "%s", campos[1]);
                rede->sinal = atoi(campos[2]);
                snprintf(rede->seguranca, sizeof(rede->seguranca), "%.*s",
                         (int) sizeof(rede->seguranca) - 1, campos[3]);
                rede->em_uso = strstr(campos[0], "*") != NULL;
                r->n_redes++;
            }
            linha = strtok(NULL, "\n");
        }
        wm_log("radio: %d rede(s) encontrada(s)", r->n_redes);
    }
    r->ocupado = false;
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

void radio_scan_wifi(Radio *r)
{
    if (!r) {
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (r->ocupado) {
        pthread_mutex_unlock(&r->lock);
        return;
    }
    r->ocupado = true;
    r->ultimo_erro[0] = '\0';
    snprintf(r->ultimo_aviso, sizeof(r->ultimo_aviso), "Procurando redes...");
    if (pthread_create(&r->thread, NULL, thread_scan_wifi, r) != 0) {
        r->ocupado = false;
        snprintf(r->ultimo_erro, sizeof(r->ultimo_erro), "nao consegui iniciar a busca");
    }
    pthread_mutex_unlock(&r->lock);
}

int radio_redes(Radio *r, struct Rede *out, int max)
{
    int n = 0;

    if (!r) {
        return 0;
    }
    pthread_mutex_lock(&r->lock);
    for (int i = 0; i < r->n_redes && i < max; i++) {
        out[i] = r->redes[i];
        n++;
    }
    pthread_mutex_unlock(&r->lock);
    return n;
}

bool radio_ocupado(Radio *r)
{
    bool ocupado;

    if (!r) {
        return false;
    }
    pthread_mutex_lock(&r->lock);
    ocupado = r->ocupado;
    pthread_mutex_unlock(&r->lock);
    return ocupado;
}

void radio_ultimo_aviso(Radio *r, char *out, size_t size)
{
    if (!r || !out) {
        return;
    }
    pthread_mutex_lock(&r->lock);
    snprintf(out, size, "%s", r->ultimo_erro[0] ? r->ultimo_erro : r->ultimo_aviso);
    pthread_mutex_unlock(&r->lock);
}

/* ------------------------------------------------------------- conectar --- */

typedef struct {
    Radio *radio;
    char ssid[64];
    char senha[128];
    int acao;   /* 0 = wifi connect, 1 = bt power on/off, 2 = bt scan, 3 = bt connect */
    bool bt_on;
    char mac[32];
} PedidoRadio;

static void *thread_pedido(void *arg)
{
    PedidoRadio *p = arg;
    char saida[2048];
    int rc = -1;

    if (p->acao == 0) {
        const char *argv[8];
        int n = 0;
        argv[n++] = "nmcli";
        argv[n++] = "device";
        argv[n++] = "wifi";
        argv[n++] = "connect";
        argv[n++] = p->ssid;
        if (p->senha[0]) {
            argv[n++] = "password";
            argv[n++] = p->senha;
        }
        argv[n] = NULL;
        rc = executar(argv, saida, sizeof(saida));
    } else if (p->acao == 1) {
        const char *argv[] = { "bluetoothctl", "power", p->bt_on ? "on" : "off", NULL };
        rc = executar(argv, saida, sizeof(saida));
    } else if (p->acao == 2) {
        const char *argv[] = { "bluetoothctl", "scan", "on", NULL };
        rc = executar(argv, saida, sizeof(saida));
    } else if (p->acao == 3) {
        const char *argv[] = { "bluetoothctl", "connect", p->mac, NULL };
        rc = executar(argv, saida, sizeof(saida));
    } else if (p->acao == 4) {
        const char *argv[] = { "bluetoothctl", "discoverable",
                               p->bt_on ? "on" : "off", NULL };
        rc = executar(argv, saida, sizeof(saida));
    }

    pthread_mutex_lock(&p->radio->lock);
    if (rc == 0) {
        if (p->acao == 0) {
            snprintf(p->radio->ultimo_aviso, sizeof(p->radio->ultimo_aviso),
                     "Conectado em %s", p->ssid);
        } else {
            snprintf(p->radio->ultimo_aviso, sizeof(p->radio->ultimo_aviso), "Feito");
        }
    } else {
        if (p->acao == 0) {
            snprintf(p->radio->ultimo_erro, sizeof(p->radio->ultimo_erro),
                     "Falha ao conectar em %s", p->ssid);
        } else if (p->acao == 1) {
            snprintf(p->radio->ultimo_erro, sizeof(p->radio->ultimo_erro),
                     "Falha ao mudar o Bluetooth");
        } else {
            snprintf(p->radio->ultimo_erro, sizeof(p->radio->ultimo_erro),
                     "Falha na operacao");
        }
    }
    p->radio->ocupado = false;
    pthread_mutex_unlock(&p->radio->lock);
    free(p);
    return NULL;
}

static void disparar(Radio *r, PedidoRadio *p)
{
    if (!r || !p) {
        return;
    }
    p->radio = r;
    pthread_mutex_lock(&r->lock);
    if (r->ocupado) {
        pthread_mutex_unlock(&r->lock);
        free(p);
        return;
    }
    r->ocupado = true;
    r->ultimo_erro[0] = '\0';
    r->ultimo_aviso[0] = '\0';
    if (pthread_create(&r->thread, NULL, thread_pedido, p) != 0) {
        r->ocupado = false;
        free(p);
    }
    pthread_mutex_unlock(&r->lock);
}

void radio_conectar_wifi(Radio *r, const char *ssid, const char *senha)
{
    PedidoRadio *p = calloc(1, sizeof(*p));

    if (!p) {
        return;
    }
    p->acao = 0;
    snprintf(p->ssid, sizeof(p->ssid), "%.*s", (int) sizeof(p->ssid) - 1, ssid ? ssid : "");
    snprintf(p->senha, sizeof(p->senha), "%.*s", (int) sizeof(p->senha) - 1, senha ? senha : "");
    disparar(r, p);
}

void radio_bt_power(Radio *r, bool ligar)
{
    PedidoRadio *p = calloc(1, sizeof(*p));

    if (!p) {
        return;
    }
    p->acao = 1;
    p->bt_on = ligar;
    disparar(r, p);
}

void radio_bt_scan(Radio *r)
{
    PedidoRadio *p = calloc(1, sizeof(*p));

    if (!p) {
        return;
    }
    p->acao = 2;
    disparar(r, p);
}

void radio_bt_connect(Radio *r, const char *mac)
{
    PedidoRadio *p = calloc(1, sizeof(*p));

    if (!p) {
        return;
    }
    p->acao = 3;
    snprintf(p->mac, sizeof(p->mac), "%.*s", (int) sizeof(p->mac) - 1, mac ? mac : "");
    disparar(r, p);
}

int radio_bt_dispositivos(Radio *r, struct BtDevice *out, int max)
{
    int n;

    if (!r || !out || max <= 0) {
        return 0;
    }
    pthread_mutex_lock(&r->lock);
    n = r->n_bt_devs < max ? r->n_bt_devs : max;
    memcpy(out, r->bt_devs, (size_t) n * sizeof(*out));
    pthread_mutex_unlock(&r->lock);
    return n;
}

void radio_bt_visivel(Radio *r, bool ligar)
{
    Radio *radio = r;
    PedidoRadio *p;

    if (!radio) {
        return;
    }
    p = calloc(1, sizeof(*p));
    if (!p) {
        return;
    }
    p->acao = 4;
    p->bt_on = ligar;
    disparar(radio, p);
}
