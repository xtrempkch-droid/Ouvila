/* Walkmam Media — app de mídia único para os handhelds RK3566.
 *
 * Este programa é a camada inteira: desenha no DRM/KMS, decodifica com as
 * bibliotecas do FFmpeg, toca no ALSA e lê o controle/gamepad por evdev. Não
 * depende de compositor (Weston/Sway), navegador (WebKit/cog), player externo
 * (mpv/ffplay) nem de um backend HTTP: é um binário só, que abre o dispositivo
 * de vídeo e a placa de som directamente.
 *
 * Layout do módulo:
 *   util.c     log, arquivos, configuração (/boot/walkmam.conf), textos
 *   gfx.c      canvas XRGB8888: retângulos, cantos arredondados, gradiente, alpha
 *   font.c     FreeType + cache de glifos (UTF-8)
 *   drm.c      KMS: dumb buffer, page flip, rotação por software
 *   audio.c    ALSA: saída S16 estéreo, volume, relógio de reprodução
 *   decoder.c  FFmpeg: demux, decode de áudio/vídeo, sincronia, seek
 *   library.c  varredura de mídia em segundo plano (com tags)
 *   input.c    evdev: gamepad rocknix + teclado USB -> ações lógicas
 *   ui.c       telas, navegação e desenho
 *   main.c     inicialização, laço principal, modos de linha de comando
 */

#ifndef WM_H
#define WM_H

/* _DEFAULT_SOURCE, e NÃO _GNU_SOURCE: no glibc >= 2.38 o _GNU_SOURCE liga as
 * redireções C23 (`__isoc23_strtol`, `__isoc23_sscanf`, `__isoc23_fscanf`), que
 * passam a exigir GLIBC_2.38 em tempo de execução. A imagem roda bookworm
 * (glibc 2.36), então o binário compilado no runner morria com
 *   version `GLIBC_2.38' not found (required by /usr/local/bin/walkmam-media)
 * _DEFAULT_SOURCE dá POSIX.1-2008 + BSD (o que estes arquivos usam: O_CLOEXEC,
 * clock_gettime, localtime_r, execlp, ...) sem pedir C23. */
#define _DEFAULT_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef WM_VERSION
#define WM_VERSION "1.0.0"
#endif

/* --------------------------------------------------------------- util --- */

#define WM_PATH_MAX 512

typedef struct {
    char conf_path[WM_PATH_MAX];
    char log_path[WM_PATH_MAX];
    char ui_note[128];
    char transform[24];
    char audio_device[64];
    char media_roots[WM_PATH_MAX];
    int initial_volume;
    int brightness;
    bool exit_poweroff;
    bool log_to_file;
} WmConfig;

void wm_log_open(const char *path);
void wm_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void wm_log_close(void);

/* Lê "chave=valor" de walkmam.conf; CR do FAT é removido. */
void wm_conf_read(WmConfig *cfg, const char *path);
/* "auto"/vazio -> decide pelo /proc/device-tree/model. */
const char *wm_transform_for_model(const char *requested);
void wm_media_roots(const char *spec, char out[][WM_PATH_MAX], int *count, int max);
double wm_now(void);
void wm_fmt_time(double segundos, char *out, size_t size);
void wm_fmt_bytes(double bytes, char *out, size_t size);
void wm_mkdir_p(const char *path);
bool wm_read_file(const char *path, char *out, size_t size);
int wm_utf8_decode(const char *s, uint32_t *cp);
void wm_trim(char *s);
int wm_casecmp(const char *a, const char *b);

/* ---------------------------------------------------------------- gfx --- */

typedef struct {
    uint32_t *px;
    int w;
    int h;
} Canvas;

typedef struct {
    uint8_t r, g, b, a;
} Color;

typedef struct {
    int x, y, w, h;
} Rect;

static inline Color wm_rgba(int r, int g, int b, int a)
{
    Color c = { (uint8_t) r, (uint8_t) g, (uint8_t) b, (uint8_t) a };
    return c;
}

static inline Color wm_rgb(int r, int g, int b)
{
    return wm_rgba(r, g, b, 255);
}

static inline bool rect_contains(Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void canvas_init(Canvas *c, int w, int h);
void canvas_free(Canvas *c);
void canvas_clear(Canvas *c, Color cor);
void gfx_fill(Canvas *c, Rect r, Color cor);
void gfx_fill_round(Canvas *c, Rect r, int raio, Color cor);
void gfx_stroke_round(Canvas *c, Rect r, int raio, Color cor, int espessura);
void gfx_gradient_v(Canvas *c, Rect r, int raio, Color topo, Color base);
void gfx_gradient_h(Canvas *c, Rect r, Color esq, Color dir);
void gfx_blit(Canvas *c, int x, int y, int w, int h,
              const uint32_t *src, int src_w, int src_h);
void gfx_blit_round(Canvas *c, Rect r, int raio, const uint32_t *src,
                    int src_w, int src_h);
void gfx_shadow_bottom(Canvas *c, Rect r, int altura, uint8_t alfa_max);

/* --------------------------------------------------------------- font --- */

typedef struct Font Font;

/* Abre um TTF. Devolve NULL se o arquivo não existir (a UI cai num desenho
 * substituto em vez de morrer). */
Font *font_open(const char *path, int tamanho_px);
void font_close(Font *f);
int font_line_height(const Font *f);
int font_ascent(const Font *f);
int font_text_width(const Font *f, const char *texto);
/* Desenha e devolve o avanço horizontal. Corta em max_w (0 = sem limite) e
 * acrescenta "…" quando corta. */
int font_draw(Canvas *c, const Font *f, int x, int y, Color cor,
              const char *texto, int max_w);

/* ----------------------------------------------------------------- drm --- */

typedef struct Drm Drm;

struct DrmInfo {
    int fb_w, fb_h;      /* tamanho NATIVO do painel (o framebuffer) */
    int ui_w, ui_h;      /* tamanho da UI depois da rotação */
    char connector[64];
    char mode[32];
};

Drm *drm_open(struct DrmInfo *info, const char *transform);
void drm_close(Drm *d);
/* Copia o canvas para o framebuffer aplicando a rotação e faz page flip
 * (bloqueante, espera o vblank). Devolve false se o flip falhou. */
bool drm_present(Drm *d, const Canvas *c);
/* Liga/desliga a saída (propriedade DPMS do conector). Desligar a saída é o que
 * economiza bateria de verdade: o painel para de ser varrido. */
bool drm_dpms(Drm *d, bool ligado);
/* Quantas vezes o flip falhou (0 = saudável). */
int drm_falhas(const Drm *d);

/* --------------------------------------------------------------- audio --- */

typedef struct AudioOut AudioOut;

struct AudioStats {
    bool open;
    int rate;
    int channels;
    char device[64];
    uint64_t frames_written;
};

AudioOut *audio_open(const char *device, int rate, int channels);
void audio_close(AudioOut *a);
/* Escreve blocos S16 entrelaçados. Bloqueia quando o buffer do ALSA está cheio
 * — é o que dá o ritmo da reprodução. */
int audio_write(AudioOut *a, const int16_t *dados, int frames);
void audio_set_volume(AudioOut *a, int volume_0_100);
int audio_volume(AudioOut *a);
void audio_pause(AudioOut *a, bool pausado);
void audio_flush(AudioOut *a);
/* Segundos de áudio já entregues ao hardware (relógio da reprodução). */
double audio_clock(AudioOut *a);
bool audio_ok(const AudioOut *a);
void audio_stats(const AudioOut *a, struct AudioStats *out);

/* ------------------------------------------------------------- decoder --- */

typedef struct Player Player;

struct PlayerStatus {
    bool aberto;
    bool tocando;
    bool pausado;
    bool terminou;
    bool tem_video;
    bool tem_audio;
    double duracao;
    double posicao;
    char titulo[192];
    char artista[192];
    char album[192];
    int largura;
    int altura;
    char erro[160];
};

/* Abre e começa a tocar. audio_out pode ser NULL (só vídeo, sem som). */
Player *player_open(const char *arquivo, AudioOut *audio_out,
                    int video_max_w, int video_max_h, int volume);
void player_close(Player *p);
void player_status(Player *p, struct PlayerStatus *out);
void player_toggle_pause(Player *p);
void player_seek(Player *p, double segundos);
void player_set_volume(Player *p, int volume);
/* Copia o quadro de vídeo atual para dst (letterbox em dst_w x dst_h).
 * Devolve false quando não há quadro novo. */
bool player_copy_video(Player *p, uint32_t *dst, int dst_w, int dst_h);
bool player_video_dirty(Player *p);

/* ------------------------------------------------------------- library --- */

enum { WM_MIDIA_AUDIO = 0, WM_MIDIA_VIDEO = 1 };

struct MediaItem {
    char caminho[WM_PATH_MAX];
    char titulo[192];
    char artista[192];
    char album[192];
    int tipo;
    double duracao;
    int64_t tamanho;
    bool tem_capa;
};

typedef struct Library Library;

Library *library_create(const char *const *raizes, int n_raizes);
void library_destroy(Library *lib);
/* Dispara a varredura em segundo plano. */
void library_scan(Library *lib);
bool library_scanning(Library *lib);
int library_count(Library *lib);
/* Muda quando uma varredura termina (a UI usa para remontar as listas). */
unsigned library_version(Library *lib);
bool library_item(Library *lib, int indice, struct MediaItem *out);
/* Caminho da capa (folder.jpg/cover.png no diretório), se existir. */
bool library_cover_path(const struct MediaItem *item, char *out, size_t size);

/* --------------------------------------------------------------- rádio --- */

struct Rede {
    char ssid[64];
    int sinal;
    char seguranca[24];
    bool em_uso;
};

typedef struct Radio Radio;

Radio *radio_create(void);
void radio_destroy(Radio *r);
void radio_status(Radio *r, bool *wifi_presente, bool *wifi_conectado,
                  char *ssid, size_t ssid_size, char *ip, size_t ip_size,
                  bool *bt_presente, bool *bt_ligado, char *bt_mac,
                  size_t bt_mac_size);
void radio_scan_wifi(Radio *r);
int radio_redes(Radio *r, struct Rede *out, int max);
bool radio_ocupado(Radio *r);
void radio_ultimo_aviso(Radio *r, char *out, size_t size);
void radio_conectar_wifi(Radio *r, const char *ssid, const char *senha);
void radio_bt_power(Radio *r, bool ligar);
void radio_bt_scan(Radio *r);
void radio_bt_connect(Radio *r, const char *mac);

/* Piso de brilho do backlight (%). Abaixo disso o painel fica ilegível; para
 * economizar bateria existe o DPMS off ("apagar a tela"). */
#define WM_BRILHO_MIN 10

/* --------------------------------------------------------------- input --- */
enum {
    WM_KEY_NENHUMA = 0,
    WM_KEY_CIMA,
    WM_KEY_BAIXO,
    WM_KEY_ESQ,
    WM_KEY_DIR,
    WM_KEY_OK,
    WM_KEY_VOLTAR,
    WM_KEY_TOCAR_PAUSAR,
    WM_KEY_ABA_PROX,
    WM_KEY_ABA_ANT,
    WM_KEY_VOL_MAIS,
    WM_KEY_VOL_MENOS,
    WM_KEY_MENU,
    WM_KEY_PG_BAIXO,
    WM_KEY_PG_CIMA,
    WM_KEY_SAIR,
    WM_KEY_APAGAR,
    WM_KEY_ENTER,
    WM_KEY_PULO_PROX,    /* L1/R1: próxima faixa/álbum; no vídeo, avançar */
    WM_KEY_PULO_ANT,     /* L1/R1: faixa/álbum anterior; no vídeo, voltar */
    WM_KEY_POWER_CURTO,  /* pwrkey: toque = apagar/acordar a tela */
    WM_KEY_POWER_LONGO   /* pwrkey segurado >= 3 s: diálogo de desligar */
};

struct KeyEvent {
    int key;
    bool soltou;    /* tecla liberada (value == 0) — não gera ação */
    bool usb;        /* veio de teclado USB, não do gamepad */
};

typedef struct Input Input;

Input *input_open(void);
void input_close(Input *in);
/* Espera até timeout_ms por eventos. Devolve quantos preencheu em `out`. */
int input_poll(Input *in, struct KeyEvent *out, int max, int timeout_ms);
/* Nome do gamepad encontrado (ou "(nenhum)"). */
const char *input_gamepad_name(const Input *in);
const char *input_keyboard_name(const Input *in);

/* ------------------------------------------------------------------ ui --- */

struct SystemInfo {
    int bateria;          /* -1 = sem leitura */
    bool carregando;
    bool tem_bateria;
    bool wifi_presente;
    bool wifi_conectado;
    char wifi_nome[64];
    char wifi_ip[32];
    bool bt_presente;
    bool bt_ligado;
    char bt_mac[32];
    int brilho;           /* -1 = sem backlight */
    long uptime;
    char modelo[96];
};

typedef struct Ui Ui;

Ui *ui_create(Canvas *canvas, Library *lib, AudioOut *audio);
void ui_destroy(Ui *u);
/* Migalhas que a UI precisa do main (estado do player/sistema). */
void ui_set_player(Ui *u, Player *p);
void ui_update_system(Ui *u, const struct SystemInfo *sys);
/* Trata uma tecla. Devolve a ação pedida ao main. */
enum {
    WM_ACAO_NENHUMA = 0,
    WM_ACAO_SAIR,
    WM_ACAO_DESLIGAR,
    WM_ACAO_REINICIAR,
    WM_ACAO_RESSCAN,
    WM_ACAO_BRILHO,
    WM_ACAO_TELA_APAGAR,
    WM_ACAO_TELA_ACENDER
};
int ui_handle_key(Ui *u, const struct KeyEvent *ev);
/* Brilho atual do backlight em %, -1 quando não há backlight. Usado para
 * restaurar o nível ao acender a tela depois do DPMS off. */
int ui_brilho(Ui *u);
/* true quando a tela está apagada (DPMS off): o main deixa de desenhar. */
bool ui_tela_apagada(Ui *u);
/* Pedidos de mídia que a UI faz (tocar a faixa N da lista filtrada, etc.). */
const char *ui_want_play(Ui *u, double *seek_to);
double ui_want_seek(Ui *u, bool *absoluto);
int ui_want_volume(Ui *u);
int ui_want_brightness(Ui *u);
bool ui_wants_toggle(Ui *u);
/* 1 = anterior, 2 = próximo, 3 = parar, 0 = nada. */
int ui_want_transport(Ui *u);
void ui_set_radio(Ui *u, Radio *r);
void ui_set_tab(Ui *u, int aba);
void ui_set_video_fullscreen(Ui *u, bool ativo);
void ui_show_osd(Ui *u, double segundos);
void ui_set_current_path(Ui *u, const char *caminho);
/* Caminho da faixa ± delta na lista atual (com volta). NULL = lista vazia. */
const char *ui_step_track(Ui *u, int delta);
void ui_refresh_library(Ui *u);
bool ui_video_fullscreen(const Ui *u);
void ui_render(Ui *u);

/* Carrega uma imagem (jpg/png/webp) como RGB32 usando o próprio FFmpeg. */
bool imagem_carregar(const char *caminho, uint32_t **px, int *w, int *h);

#endif /* WM_H */
