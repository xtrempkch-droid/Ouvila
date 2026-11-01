/* ui.c — telas, navegação e desenho da UI.
 *
 * Referência visual: variants/media-player/layout/layout1.md (fundo quase
 * preto, painéis "glass", acento vermelho). Tudo é desenhado à mão no canvas,
 * sem toolkit: assim o app continua sendo um binário só.
 *
 * Abas:
 *   Música  — capa, título/artista/álbum, progresso, controles e biblioteca
 *   Vídeos  — grade da biblioteca + reprodução em tela cheia (o quadro vem do
 *             decodificador; esta tela é o "video player" da variante)
 *   Rádio   — Wi-Fi/BT: estado do kernel + scan/conectar pelo NetworkManager
 *   Sistema — modelo, bateria, armazenamento, brilho, desligar/reiniciar
 *
 * Entrada: só o gamepad/teclado (não há toque). A navegação é vertical dentro
 * da aba e horizontal entre abas, com as teclas L/R para pular de aba.
 */

#include "wm.h"

#include <dirent.h>
#include <math.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define ITENS_LISTA_MAX 400
#define TELAS 4

enum { ABA_MUSICA = 0, ABA_VIDEOS = 1, ABA_RADIO = 2, ABA_SISTEMA = 3 };

enum {
    FOCO_LISTA = 0,
    FOCO_CONTROLES = 1
};

struct Ui {
    Canvas *canvas;
    Library *lib;
    AudioOut *audio;
    Player *player;
    Radio *radio;
    struct SystemInfo sys;
    bool quer_tela_cheia;

    Font *f_titulo;     /* 26-32 px */
    Font *f_grande;     /* 20 px */
    Font *f_texto;      /* 16 px */
    Font *f_pequeno;    /* 13 px */
    Font *f_numero;     /* 14 px monoespaçado-ish */

    int aba;
    int foco;
    int foco_botao;   /* índice dentro da barra de controles */
    int topo_lista;

    int musicas[ITENS_LISTA_MAX];
    int n_musicas;
    int videos[ITENS_LISTA_MAX];
    int n_videos;

    struct Rede redes[32];
    int n_redes;

    bool video_tela_cheia;
    bool mostrar_ajuda;
    double osd_ate;

    char botoes[4][64];
    int n_botoes;

    /* pedidos para o main */
    char tocando_path[WM_PATH_MAX];
    char quer_tocar[WM_PATH_MAX];
    bool tem_quer_tocar;
    double quer_seek;
    bool tem_quer_seek;
    int quer_volume;
    bool tem_quer_volume;
    bool quer_toggle;
    int quer_brilho;
    bool quer_anterior;
    bool quer_proximo;
    bool quer_parar;

    /* teclado na tela (senha do Wi-Fi) */
    bool teclado_aberto;
    bool maiusculas;
    char entrada[128];
    int cursor_tecla;
    char ssid_pendente[64];

    /* capa decodificada do álbum atual */
    uint32_t *capa;
    int capa_w;
    int capa_h;
    char capa_de[WM_PATH_MAX];
    /* Caminho que não decodificou: sem isso a capa quebrada seria reaberta a
     * cada quadro (o arquivo inválido custa uma tentativa de decode). */
    char capa_falhou[WM_PATH_MAX];

    unsigned versao_biblioteca;
};

/* ----------------------------------------------------------- geometria --- */

typedef struct {
    int margem;
    int topo_h;
    int rodape_h;
    int raio;
    int gap;
    bool compacto;
    int altura_linha;
} Layout;

static Layout layout_de(const Canvas *c)
{
    Layout l;

    l.compacto = c->w < 900;
    l.margem = l.compacto ? 10 : 18;
    l.topo_h = l.compacto ? 38 : 52;
    l.rodape_h = l.compacto ? 28 : 38;
    l.raio = l.compacto ? 10 : 16;
    l.gap = l.compacto ? 8 : 14;
    l.altura_linha = l.compacto ? 26 : 36;
    return l;
}

/* ---------------------------------------------------------------- cores --- */

static const Color COR_FUNDO = { 7, 9, 14, 255 };
static const Color COR_FUNDO_2 = { 13, 18, 28, 255 };
static const Color COR_PAINEL = { 22, 30, 46, 215 };
static const Color COR_PAINEL_2 = { 29, 39, 57, 235 };
static const Color COR_BORDA = { 42, 55, 78, 255 };
static const Color COR_ACENTO = { 244, 63, 94, 255 };
static const Color COR_TEXTO = { 248, 250, 252, 255 };
static const Color COR_MUTED = { 148, 163, 184, 255 };
static const Color COR_OK = { 52, 211, 153, 255 };
static const Color COR_BARRA = { 51, 65, 85, 255 };

/* ------------------------------------------------------------- helpers --- */

static void texto(Canvas *c, const Font *f, int x, int y, Color cor, const char *s)
{
    if (f) {
        font_draw(c, f, x, y, cor, s, 0);
    }
}

static void texto_limitado(Canvas *c, const Font *f, int x, int y, Color cor,
                           const char *s, int max_w)
{
    if (f) {
        font_draw(c, f, x, y, cor, s, max_w);
    }
}

static int largura_texto(const Font *f, const char *s)
{
    return f ? font_text_width(f, s) : 0;
}

/* Faixa de gradiente do fundo (dá profundidade sem imagem). */
static void pintar_fundo(Ui *u)
{
    Canvas *c = u->canvas;

    canvas_clear(c, COR_FUNDO);
    gfx_gradient_v(c, (Rect) { 0, 0, c->w, c->h / 2 }, 0, COR_FUNDO_2, COR_FUNDO);
    gfx_gradient_h(c, (Rect) { 0, 0, c->w, c->h }, wm_rgba(20, 28, 44, 90),
                   wm_rgba(7, 9, 14, 0));
}

static void barra_progresso(Canvas *c, Rect r, double fracao, Color cor)
{
    int preenchido;

    gfx_fill_round(c, r, r.h / 2, COR_BARRA);
    if (fracao < 0) {
        fracao = 0;
    }
    if (fracao > 1) {
        fracao = 1;
    }
    preenchido = (int) (r.w * fracao);
    if (preenchido > 2) {
        gfx_fill_round(c, (Rect) { r.x, r.y, preenchido, r.h }, r.h / 2, cor);
    }
}

/* Pílula de status: bolinha + rótulo. */
static int chip(Canvas *c, const Font *f, int x, int y, const char *rotulo,
                Color cor, bool ativo)
{
    int altura = 20;
    int w = 20 + largura_texto(f, rotulo);
    Rect r = { x, y, w, altura };

    gfx_fill_round(c, r, altura / 2, ativo ? wm_rgba(0, 0, 0, 70) : wm_rgba(0, 0, 0, 90));
    gfx_stroke_round(c, r, altura / 2, wm_rgba(255, 255, 255, ativo ? 40 : 22), 1);
    gfx_fill_round(c, (Rect) { x + 7, y + 7, 6, 6 }, 3, ativo ? cor : COR_MUTED);
    texto(c, f, x + 17, y + altura / 2 + font_ascent(f) / 2 - 2,
          ativo ? COR_TEXTO : COR_MUTED, rotulo);
    return w;
}

/* ---------------------------------------------------------- capa do CD --- */

/* Decodifica a imagem da capa com o próprio FFmpeg (o mesmo libav* que já
 * está ligado no binário) e guarda como RGB32. Feito uma vez por álbum. */
static bool carregar_capa(Ui *u, const char *caminho)
{
    uint32_t *px = NULL;
    int w = 0, h = 0;

    if (!caminho || !caminho[0]) {
        return false;
    }
    if (strcmp(u->capa_de, caminho) == 0 && u->capa) {
        return true;
    }
    if (strcmp(u->capa_falhou, caminho) == 0) {
        return false;
    }
    free(u->capa);
    u->capa = NULL;
    u->capa_w = u->capa_h = 0;
    if (!imagem_carregar(caminho, &px, &w, &h)) {
        snprintf(u->capa_falhou, sizeof(u->capa_falhou), "%s", caminho);
        return false;
    }
    u->capa = px;
    u->capa_w = w;
    u->capa_h = h;
    snprintf(u->capa_de, sizeof(u->capa_de), "%s", caminho);
    return true;
}

/* ----------------------------------------------------------- listas ----- */

static void montar_listas(Ui *u)
{
    int total = library_count(u->lib);
    struct MediaItem item;

    u->n_musicas = 0;
    u->n_videos = 0;
    for (int i = 0; i < total; i++) {
        if (!library_item(u->lib, i, &item)) {
            continue;
        }
        if (item.tipo == WM_MIDIA_AUDIO && u->n_musicas < ITENS_LISTA_MAX) {
            u->musicas[u->n_musicas++] = i;
        } else if (item.tipo == WM_MIDIA_VIDEO && u->n_videos < ITENS_LISTA_MAX) {
            u->videos[u->n_videos++] = i;
        }
    }
}

static int itens_da_aba(const Ui *u)
{
    switch (u->aba) {
    case ABA_MUSICA:
        return u->n_musicas;
    case ABA_VIDEOS:
        return u->n_videos;
    default:
        return 0;
    }
}

static bool item_da_aba(Ui *u, int posicao, struct MediaItem *out)
{
    int indice;

    if (posicao < 0) {
        return false;
    }
    if (u->aba == ABA_MUSICA) {
        if (posicao >= u->n_musicas) {
            return false;
        }
        indice = u->musicas[posicao];
    } else if (u->aba == ABA_VIDEOS) {
        if (posicao >= u->n_videos) {
            return false;
        }
        indice = u->videos[posicao];
    } else {
        return false;
    }
    return library_item(u->lib, indice, out);
}

/* --------------------------------------------------------------- topo --- */

static void desenhar_topo(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    static const char *nomes[TELAS] = { "Música", "Vídeos", "Rádio", "Sistema" };
    int x = l.margem;
    int y = l.margem / 2 + 4;
    int chip_x;
    char buf[64];

    gfx_fill(c, (Rect) { 0, 0, c->w, l.topo_h }, wm_rgba(0, 0, 0, 60));
    gfx_fill(c, (Rect) { 0, l.topo_h - 1, c->w, 1 }, wm_rgba(255, 255, 255, 18));

    for (int i = 0; i < TELAS; i++) {
        Font *f = l.compacto ? u->f_pequeno : u->f_texto;
        int largura = largura_texto(f, nomes[i]) + (l.compacto ? 16 : 26);
        Rect r = { x, y, largura, l.topo_h - 2 * y + 4 };
        bool ativa = (i == u->aba);

        if (ativa) {
            gfx_fill_round(c, r, 9, wm_rgba(255, 255, 255, 22));
            gfx_stroke_round(c, r, 9, wm_rgba(255, 255, 255, 35), 1);
        }
        texto(c, f, r.x + (l.compacto ? 8 : 13), r.y + r.h / 2 + font_ascent(f) / 2 - 2,
              ativa ? COR_TEXTO : COR_MUTED, nomes[i]);
        x += largura + 4;
    }

    /* Status à direita: relógio, bateria, Wi-Fi, BT — desenhados da direita
     * para a esquerda, então a posição não depende de largura fixa. */
    {
        Font *f = l.compacto ? u->f_pequeno : u->f_texto;
        time_t agora = time(NULL);
        struct tm tm;
        int base = y + l.topo_h / 2 + font_ascent(f) / 2 - 8;

        localtime_r(&agora, &tm);
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        chip_x = c->w - l.margem - largura_texto(f, buf);
        texto(c, f, chip_x, base, COR_TEXTO, buf);
        chip_x -= l.compacto ? 8 : 14;

        if (u->sys.tem_bateria && u->sys.bateria >= 0) {
            snprintf(buf, sizeof(buf), "%d%%", u->sys.bateria);
            chip_x -= largura_texto(u->f_pequeno, buf);
            texto(c, u->f_pequeno, chip_x, base + 2,
                  u->sys.carregando ? COR_OK : COR_MUTED, buf);
            chip_x -= l.compacto ? 8 : 14;
        }

        chip_x -= largura_texto(u->f_pequeno, "bt off") + 20;
        chip(c, u->f_pequeno, chip_x, base - 6,
             u->sys.bt_ligado ? "bt on" : "bt off", COR_OK, u->sys.bt_ligado);
        chip_x -= l.compacto ? 4 : 8;

        chip_x -= largura_texto(u->f_pequeno, "wifi off") + 20;
        snprintf(buf, sizeof(buf), "%s", u->sys.wifi_conectado ? "wifi" : "wifi off");
        chip(c, u->f_pequeno, chip_x, base - 6, buf, COR_OK, u->sys.wifi_conectado);
    }
}

/* ------------------------------------------------------------- rodapé --- */

static void desenhar_rodape(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    int y = c->h - l.rodape_h;
    struct PlayerStatus st;
    char buf[256];

    gfx_fill(c, (Rect) { 0, y, c->w, 1 }, wm_rgba(255, 255, 255, 18));

    memset(&st, 0, sizeof(st));
    if (u->player) {
        player_status(u->player, &st);
    }

    if (l.compacto) {
        snprintf(buf, sizeof(buf), "A abre  B volta  L/R aba  X play/pause");
        texto(c, u->f_pequeno, l.margem, y + l.rodape_h / 2 + 4, COR_MUTED, buf);
        snprintf(buf, sizeof(buf), "%s", st.aberto ? st.titulo : "");
        texto_limitado(c, u->f_pequeno, c->w / 2, y + l.rodape_h / 2 + 4, COR_TEXTO,
                       buf, c->w / 2 - l.margem - 40);
        snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
        texto(c, u->f_pequeno, c->w - l.margem - 34, y + l.rodape_h / 2 + 4,
              COR_MUTED, buf);
        return;
    }

    texto(c, u->f_pequeno, l.margem, y + l.rodape_h / 2 + 5, COR_MUTED,
          "A selecionar   B voltar   L/R trocar de aba   X tocar/pausar   Y menu");

    /* Mini player centralizado à direita. */
    {
        char titulo[128];
        int x = c->w - l.margem;

        snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
        x -= largura_texto(u->f_pequeno, buf);
        texto(c, u->f_pequeno, x, y + l.rodape_h / 2 + 5, COR_MUTED, buf);
        x -= 14;

        snprintf(titulo, sizeof(titulo), "%.*s", (int) sizeof(titulo) - 1,
                 st.aberto ? st.titulo : "nada tocando");
        if (strlen(titulo) > 46) {
            titulo[46] = '\0';
        }
        x -= largura_texto(u->f_pequeno, titulo);
        texto(c, u->f_pequeno, x, y + l.rodape_h / 2 + 5, COR_TEXTO, titulo);
        x -= 12;

        gfx_fill_round(c, (Rect) { x - 7, y + l.rodape_h / 2, 7, 7 }, 4,
                       st.tocando ? COR_OK : COR_ACENTO);
    }
}

/* -------------------------------------------------------------- música --- */

static void desenhar_painel_musica(Ui *u, Layout l, Rect area)
{
    Canvas *c = u->canvas;
    struct PlayerStatus st;
    struct MediaItem atual;
    bool tem_item = false;
    int capa_tam = l.compacto ? 96 : 168;
    int x = area.x + (l.compacto ? 12 : 20);
    int y = area.y + (l.compacto ? 12 : 20);
    char buf[128];

    memset(&st, 0, sizeof(st));
    if (u->player) {
        player_status(u->player, &st);
    }
    /* A capa vem da pasta da faixa em reprodução; sem reprodução, da faixa
     * destacada na lista (é o que se espera ao navegar pela biblioteca). */
    for (int i = 0; i < itens_da_aba(u) && !tem_item; i++) {
        if (item_da_aba(u, i, &atual) && u->tocando_path[0]
            && strcmp(atual.caminho, u->tocando_path) == 0) {
            tem_item = true;
        }
    }
    if (!tem_item && item_da_aba(u, u->cursor_tecla, &atual)) {
        tem_item = true;
    }

    gfx_fill_round(c, area, l.raio, COR_PAINEL);
    gfx_stroke_round(c, area, l.raio, wm_rgba(255, 255, 255, 18), 1);

    /* Etiqueta de estado acima da capa. */
    if (st.aberto && st.tocando) {
        snprintf(buf, sizeof(buf), "REPRODUZINDO");
    } else if (st.aberto) {
        snprintf(buf, sizeof(buf), "PAUSADO");
    } else {
        snprintf(buf, sizeof(buf), "PARADO");
    }
    {
        int etiqueta_w = largura_texto(u->f_pequeno, buf) + 18;

        gfx_fill_round(c, (Rect) { x, y, etiqueta_w, 20 }, 10, wm_rgba(244, 63, 94, 45));
        texto(c, u->f_pequeno, x + 9, y + 15, COR_ACENTO, buf);
        y += 30;
    }

    /* Capa do álbum (ou um disco desenhado, quando não há imagem). */
    {
        Rect capa = { x, y, capa_tam, capa_tam };
        char capa_path[WM_PATH_MAX];

        gfx_fill_round(c, capa, l.raio, COR_PAINEL_2);
        if (tem_item && library_cover_path(&atual, capa_path, sizeof(capa_path))
            && carregar_capa(u, capa_path)) {
            gfx_blit_round(c, capa, l.raio, u->capa, u->capa_w, u->capa_h);
        } else {
            int cx = capa.x + capa.w / 2;
            int cy = capa.y + capa.h / 2;
            int r = capa.w / 3;

            gfx_gradient_v(c, capa, l.raio, wm_rgba(67, 56, 202, 255),
                           wm_rgba(244, 63, 94, 255));
            for (int dy = -r; dy <= r; dy++) {
                int larg = (int) sqrt((double) (r * r - dy * dy));
                gfx_fill(c, (Rect) { cx - larg, cy + dy, 2 * larg, 1 },
                         wm_rgba(0, 0, 0, 70));
            }
            gfx_fill_round(c, (Rect) { cx - r / 5, cy - r / 5, 2 * r / 5, 2 * r / 5 },
                           r / 5, COR_FUNDO);
        }
        y += capa_tam + (l.compacto ? 10 : 16);
    }

    /* Título / artista / álbum. */
    {
        int max = area.x + area.w - x - (l.compacto ? 12 : 20);

        texto_limitado(c, u->f_titulo, x, y + font_ascent(u->f_titulo), COR_TEXTO,
                       st.aberto ? st.titulo : "Nada tocando", max);
        y += font_line_height(u->f_titulo) + 2;
        texto_limitado(c, u->f_texto, x, y + font_ascent(u->f_texto), COR_MUTED,
                       st.aberto ? (st.artista[0] ? st.artista : "artista desconhecido")
                                 : "Copie músicas para /media na partição BOOT",
                       max);
        y += font_line_height(u->f_texto) + 2;
        if (st.aberto && st.album[0]) {
            texto_limitado(c, u->f_pequeno, x, y + font_ascent(u->f_pequeno),
                           wm_rgba(148, 163, 184, 200), st.album, max);
        }
    }

    /* Progresso e tempos, colados no rodapé do painel. */
    {
        int py = area.y + area.h - (l.compacto ? 22 : 30);
        int pw = area.w - 2 * (l.compacto ? 12 : 20);
        char esquerda[16], direita[16];

        wm_fmt_time(st.posicao, esquerda, sizeof(esquerda));
        wm_fmt_time(st.duracao, direita, sizeof(direita));
        texto(c, u->f_pequeno, x, py - 8, COR_MUTED, esquerda);
        texto(c, u->f_pequeno, x + pw - largura_texto(u->f_pequeno, direita), py - 8,
              COR_MUTED, direita);
        barra_progresso(c, (Rect) { x, py + 6, pw, 6 }, 0, COR_ACENTO);
        if (st.duracao > 0) {
            double fracao = st.posicao / st.duracao;
            int preenchido = (int) (pw * fracao);

            if (preenchido > 3) {
                gfx_fill_round(c, (Rect) { x, py + 6, preenchido, 6 }, 3, COR_ACENTO);
            }
        }
    }
}

/* Barra de controles com os botões desenhados (não há widgets nativos). */
static void desenhar_controles(Ui *u, Layout l, Rect area, bool com_foco)
{
    Canvas *c = u->canvas;
    static const char *rotulos[] = { "<<", "PLAY", ">>", "STOP", "VOL-" , "VOL+" };
    int n = 6;
    int largura = l.compacto ? 54 : 78;
    int altura = l.compacto ? 24 : 32;
    int x = area.x + 12;
    int y = area.y + 10;
    int passo = largura + (l.compacto ? 5 : 8);

    u->n_botoes = 0;
    for (int i = 0; i < n && i < 4; i++) {
        Rect r = { x + i * passo, y, largura, altura };
        bool atual = com_foco && u->foco == FOCO_CONTROLES && u->foco_botao == i;
        bool principal = (i == 1);

        gfx_fill_round(c, r, altura / 2, principal ? COR_ACENTO : wm_rgba(255, 255, 255, 22));
        if (atual) {
            gfx_stroke_round(c, r, altura / 2, COR_TEXTO, 2);
        }
        {
            struct PlayerStatus st;
            const char *rotulo;

            memset(&st, 0, sizeof(st));
            if (u->player) {
                player_status(u->player, &st);
            }
            rotulo = (i == 1) ? (st.tocando ? "PAUSA" : "TOCAR") : rotulos[i];
            int largura_rotulo = largura_texto(u->f_pequeno, rotulo);
            texto(c, u->f_pequeno, r.x + (r.w - largura_rotulo) / 2, r.y + r.h / 2 + 5,
                  principal ? COR_TEXTO : COR_MUTED, rotulo);
            snprintf(u->botoes[u->n_botoes++], 64, "%s", rotulo);
        }
    }
    (void) n;
}

/* -------------------------------------------------------------- vídeos --- */

static void desenhar_grade_videos(Ui *u, Layout l, Rect area)
{
    Canvas *c = u->canvas;
    int colunas = l.compacto ? 2 : 3;
    int gap = l.gap;
    int larg = (area.w - 2 * gap - 2 * 12) / colunas;
    int alt = l.compacto ? 66 : 92;
    int linhas_visiveis = (area.h - 24) / (alt + gap);
    int primeiro = u->topo_lista;
    char buf[64];

    gfx_fill_round(c, area, l.raio, COR_PAINEL);
    gfx_stroke_round(c, area, l.raio, wm_rgba(255, 255, 255, 18), 1);

    snprintf(buf, sizeof(buf), "%d vídeo(s)%s", u->n_videos,
             library_scanning(u->lib) ? "  (varrendo...)" : "");
    texto(c, u->f_pequeno, area.x + 14, area.y + 20, COR_MUTED, buf);

    if (u->n_videos == 0) {
        texto(c, u->f_texto, area.x + 14, area.y + 56, COR_MUTED,
              "Nenhum vídeo encontrado.");
        texto(c, u->f_pequeno, area.x + 14, area.y + 56 + font_line_height(u->f_texto),
              wm_rgba(148, 163, 184, 200),
              "Copie os arquivos para /storage/media (Wi-Fi + scp) ou para o cartão SD.");
        return;
    }

    for (int i = 0; i < linhas_visiveis * colunas; i++) {
        int posicao = primeiro + i;
        int col = i % colunas;
        int lin = i / colunas;
        struct MediaItem item;
        Rect r;
        bool selecionado;

        if (!item_da_aba(u, posicao, &item)) {
            break;
        }
        r = (Rect) { area.x + 12 + col * (larg + gap),
                     area.y + 30 + lin * (alt + gap), larg, alt };
        selecionado = (u->foco == FOCO_LISTA && posicao == u->cursor_tecla);

        gfx_fill_round(c, r, l.raio, COR_PAINEL_2);
        if (selecionado) {
            gfx_stroke_round(c, r, l.raio, COR_ACENTO, 2);
        } else {
            gfx_stroke_round(c, r, l.raio, wm_rgba(255, 255, 255, 22), 1);
        }
        {
            char ext[12];
            char tam[32];
            const char *ponto = strrchr(item.caminho, '.');

            snprintf(ext, sizeof(ext), "%s", ponto ? ponto + 1 : "");
            for (char *p = ext; *p; p++) {
                if (*p >= 'a' && *p <= 'z') {
                    *p = (char) (*p - 'a' + 'A');
                }
            }
            wm_fmt_bytes((double) item.tamanho, tam, sizeof(tam));
            snprintf(buf, sizeof(buf), "%s · %s", ext, tam);
            texto(c, u->f_pequeno, r.x + 10, r.y + 18, COR_MUTED, buf);
        }
        texto_limitado(c, u->f_texto, r.x + 10, r.y + r.h - 12, COR_TEXTO,
                       item.titulo, r.w - 20);
        /* "play" desenhado: triângulo */
        if (selecionado) {
            int tx = r.x + r.w - 24;
            int ty = r.y + 14;
            for (int lin_t = 0; lin_t < 10; lin_t++) {
                gfx_fill(c, (Rect) { tx, ty + lin_t, lin_t + 1, 1 }, COR_ACENTO);
                gfx_fill(c, (Rect) { tx, ty + 20 - lin_t, lin_t + 1, 1 }, COR_ACENTO);
            }
        }
    }

    if (u->n_videos > linhas_visiveis * colunas) {
        snprintf(buf, sizeof(buf), "%d-%d de %d", primeiro + 1,
                 primeiro + linhas_visiveis * colunas > u->n_videos
                     ? u->n_videos
                     : primeiro + linhas_visiveis * colunas,
                 u->n_videos);
        texto(c, u->f_pequeno, area.x + area.w - 14 - largura_texto(u->f_pequeno, buf),
              area.y + 20, COR_MUTED, buf);
    }
}

/* Reprodução de vídeo em tela cheia: o quadro vem do decodificador, o OSD é
 * desenhado por cima (some sozinho depois de 3 s sem tecla). */
static void desenhar_video_tela_cheia(Ui *u)
{
    Canvas *c = u->canvas;
    struct PlayerStatus st;
    bool tem_quadro;

    memset(&st, 0, sizeof(st));
    if (u->player) {
        player_status(u->player, &st);
    }
    tem_quadro = u->player && player_copy_video(u->player, c->px, c->w, c->h);
    if (!tem_quadro) {
        canvas_clear(c, wm_rgb(0, 0, 0));
    }
    if (st.terminou) {
        /* Acabou o vídeo: volta para a biblioteca (não fica num quadro parado
         * sem indicação de que acabou). */
        u->video_tela_cheia = false;
    }

    if (u->mostrar_ajuda || wm_now() < u->osd_ate) {
        int altura = 70;
        int y = c->h - altura;
        char buf[128];

        gfx_gradient_v(c, (Rect) { 0, y, c->w, altura }, 0, wm_rgba(0, 0, 0, 40),
                       wm_rgba(0, 0, 0, 210));
        texto_limitado(c, u->f_grande, 20, y + 26, COR_TEXTO, st.titulo, c->w - 40);

        {
            char esquerda[16], direita[16];
            wm_fmt_time(st.posicao, esquerda, sizeof(esquerda));
            wm_fmt_time(st.duracao, direita, sizeof(direita));
            snprintf(buf, sizeof(buf), "%s / %s", esquerda, direita);
            texto(c, u->f_pequeno, c->w - 20 - largura_texto(u->f_pequeno, buf), y + 24,
                  COR_MUTED, buf);
        }
        barra_progresso(c, (Rect) { 20, y + 34, c->w - 40, 5 },
                        st.duracao > 0 ? st.posicao / st.duracao : 0, COR_ACENTO);
        texto(c, u->f_pequeno, 20, y + 60, COR_MUTED,
              "B sai   X pausa   ◀ ▶ avança/volta 10 s   L/R aba");
    }
}

/* --------------------------------------------------------------- rádio --- */

static void desenhar_radio(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    int meio = c->w / 2;
    Rect esquerda = { l.margem, l.topo_h + l.gap, meio - l.margem - l.gap / 2,
                      c->h - l.topo_h - l.rodape_h - 2 * l.gap };
    Rect direita = { meio + l.gap / 2, esquerda.y, esquerda.w, esquerda.h };
    char buf[128];
    char aviso[128];

    gfx_fill_round(c, esquerda, l.raio, COR_PAINEL);
    gfx_stroke_round(c, esquerda, l.raio, wm_rgba(255, 255, 255, 18), 1);
    gfx_fill_round(c, direita, l.raio, COR_PAINEL);
    gfx_stroke_round(c, direita, l.raio, wm_rgba(255, 255, 255, 18), 1);

    /* Wi-Fi */
    texto(c, u->f_texto, esquerda.x + 14, esquerda.y + 24, COR_TEXTO, "Wi-Fi");
    radio_ultimo_aviso(u->radio, aviso, sizeof(aviso));
    if (u->sys.wifi_conectado) {
        snprintf(buf, sizeof(buf), "%s  %s", u->sys.wifi_nome[0] ? u->sys.wifi_nome : "conectado",
                 u->sys.wifi_ip);
        texto_limitado(c, u->f_pequeno, esquerda.x + 14, esquerda.y + 42, COR_OK, buf,
                       esquerda.w - 28);
    } else {
        texto(c, u->f_pequeno, esquerda.x + 14, esquerda.y + 42, COR_MUTED,
              u->sys.wifi_presente ? "desconectado" : "sem rádio Wi-Fi");
    }

    {
        int y = esquerda.y + 62;
        int linhas = (esquerda.h - 76) / (l.altura_linha - 4);

        for (int i = 0; i < u->n_redes && i < linhas; i++) {
            struct Rede *rede = &u->redes[i];
            Rect r = { esquerda.x + 10, y, esquerda.w - 20, l.altura_linha - 4 };
            bool sel = (u->aba == ABA_RADIO && u->foco == FOCO_LISTA
                        && u->cursor_tecla == i);

            if (sel) {
                gfx_fill_round(c, r, 8, wm_rgba(244, 63, 94, 40));
            } else if (rede->em_uso) {
                gfx_fill_round(c, r, 8, wm_rgba(52, 211, 153, 30));
            }
            texto_limitado(c, u->f_pequeno, r.x + 8, r.y + r.h / 2 + 5, COR_TEXTO,
                           rede->ssid, r.w - 110);
            snprintf(buf, sizeof(buf), "%d%%  %s", rede->sinal,
                     rede->seguranca[0] ? rede->seguranca : "aberta");
            texto(c, u->f_pequeno, r.x + r.w - largura_texto(u->f_pequeno, buf) - 8,
                  r.y + r.h / 2 + 5, COR_MUTED, buf);
            y += l.altura_linha - 4;
        }
        if (u->n_redes == 0) {
            texto(c, u->f_pequeno, esquerda.x + 14, esquerda.y + 70, COR_MUTED,
                  radio_ocupado(u->radio) ? "Procurando..." : "Y busca redes");
        }
        if (aviso[0]) {
            texto_limitado(c, u->f_pequeno, esquerda.x + 14,
                           esquerda.y + esquerda.h - 12, COR_MUTED, aviso, esquerda.w - 28);
        }
    }

    /* Bluetooth */
    texto(c, u->f_texto, direita.x + 14, direita.y + 24, COR_TEXTO, "Bluetooth");
    snprintf(buf, sizeof(buf), "%s%s%s",
             u->sys.bt_presente ? "" : "sem rádio · ",
             u->sys.bt_ligado ? "ligado" : "desligado",
             u->sys.bt_mac[0] ? " · " : "");
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s", u->sys.bt_mac);
    texto(c, u->f_pequeno, direita.x + 14, direita.y + 42,
          u->sys.bt_ligado ? COR_OK : COR_MUTED, buf);
    texto(c, u->f_pequeno, direita.x + 14, direita.y + 66, COR_MUTED,
          "X liga/desliga · Y procura dispositivos");
    texto(c, u->f_pequeno, direita.x + 14, direita.y + 84, wm_rgba(148, 163, 184, 200),
          "Pareamento se faz pelo app do celular ou por bluetoothctl no SSH.");
}

/* ------------------------------------------------------------- sistema --- */

static void linha_dado(Canvas *c, const Font *f, Rect area, int *y,
                       const char *chave, const char *valor)
{
    texto(c, f, area.x + 14, *y, COR_MUTED, chave);
    texto_limitado(c, f, area.x + 14 + 120, *y, COR_TEXTO, valor, area.w - 160);
    *y += font_line_height(f) + 4;
}

static void desenhar_sistema(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    int meio = c->w / 2;
    Rect esquerda = { l.margem, l.topo_h + l.gap, meio - l.margem - l.gap / 2,
                      c->h - l.topo_h - l.rodape_h - 2 * l.gap };
    Rect direita = { meio + l.gap / 2, esquerda.y, esquerda.w, esquerda.h };
    char buf[128];
    int y;

    gfx_fill_round(c, esquerda, l.raio, COR_PAINEL);
    gfx_stroke_round(c, esquerda, l.raio, wm_rgba(255, 255, 255, 18), 1);
    gfx_fill_round(c, direita, l.raio, COR_PAINEL);
    gfx_stroke_round(c, direita, l.raio, wm_rgba(255, 255, 255, 18), 1);

    texto(c, u->f_texto, esquerda.x + 14, esquerda.y + 24, COR_TEXTO, "Sistema");
    y = esquerda.y + 46;
    linha_dado(c, u->f_pequeno, esquerda, &y, "Modelo",
               u->sys.modelo[0] ? u->sys.modelo : "?");
    {
        char tempo_ligado[32];
        wm_fmt_time((double) u->sys.uptime, tempo_ligado, sizeof(tempo_ligado));
        linha_dado(c, u->f_pequeno, esquerda, &y, "Ligado há", tempo_ligado);
    }
    if (u->sys.tem_bateria) {
        snprintf(buf, sizeof(buf), "%d%%%s", u->sys.bateria,
                 u->sys.carregando ? " (carregando)" : "");
        linha_dado(c, u->f_pequeno, esquerda, &y, "Bateria", buf);
    } else {
        linha_dado(c, u->f_pequeno, esquerda, &y, "Bateria", "sem leitura");
    }
    snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
    linha_dado(c, u->f_pequeno, esquerda, &y, "Volume", buf);
    linha_dado(c, u->f_pequeno, esquerda, &y, "Áudio",
               audio_ok(u->audio) ? "ALSA ok" : "sem saída ALSA");
    linha_dado(c, u->f_pequeno, esquerda, &y, "Biblioteca",
               library_scanning(u->lib) ? "varrendo..." : "pronta");

    /* Armazenamento com barra de uso. */
    texto(c, u->f_texto, direita.x + 14, direita.y + 24, COR_TEXTO, "Armazenamento");
    y = direita.y + 46;
    {
        static const struct {
            const char *rotulo;
            const char *caminho;
        } volumes[] = {
            { "Sistema", "/" },
            { "Dados (/storage)", "/storage" },
            { "Cartão 1", "/storage/sdcard1" },
            { "Cartão 2", "/storage/sdcard2" },
        };

        for (size_t i = 0; i < sizeof(volumes) / sizeof(volumes[0]); i++) {
            struct statvfs st;
            if (statvfs(volumes[i].caminho, &st) != 0 || st.f_blocks == 0) {
                continue;
            }
            {
                double total = (double) st.f_blocks * st.f_frsize;
                double livre = (double) st.f_bavail * st.f_frsize;
                char tbuf[32], lbuf[32];
                int barra_w = direita.w - 28;
                int preenchido;

                wm_fmt_bytes(total, tbuf, sizeof(tbuf));
                wm_fmt_bytes(livre, lbuf, sizeof(lbuf));
                texto(c, u->f_pequeno, direita.x + 14, y, COR_MUTED, volumes[i].rotulo);
                snprintf(buf, sizeof(buf), "%s livres de %s", lbuf, tbuf);
                texto(c, u->f_pequeno, direita.x + 14 + largura_texto(u->f_pequeno, volumes[i].rotulo) + 12,
                      y, COR_TEXTO, buf);
                y += font_line_height(u->f_pequeno);
                preenchido = (int) (barra_w * (1.0 - livre / total));
                barra_progresso(c, (Rect) { direita.x + 14, y, barra_w, 4 },
                                1.0 - livre / total, COR_ACENTO);
                (void) preenchido;
                y += font_line_height(u->f_pequeno) + 6;
            }
        }
    }

    /* Brilho e energia. */
    y = direita.y + direita.h - 74;
    texto(c, u->f_pequeno, direita.x + 14, y, COR_MUTED, "Brilho");
    if (u->sys.brilho >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", u->sys.brilho);
        texto(c, u->f_pequeno, direita.x + 90, y, COR_TEXTO, buf);
        barra_progresso(c, (Rect) { direita.x + 14, y + 10, direita.w - 28, 4 },
                        u->sys.brilho / 100.0, COR_ACENTO);
    } else {
        texto(c, u->f_pequeno, direita.x + 90, y, COR_MUTED, "sem backlight");
    }
    texto(c, u->f_pequeno, direita.x + 14, y + 34, COR_MUTED,
          "◀ ▶ brilho   Y desliga   X reinicia");
}

/* ------------------------------------------------------- teclado virtual --- */

static const char TECLAS[5][10][16] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "-" },
    { "z", "x", "c", "v", "b", "n", "m", "_", ".", "@" },
    { "MAIUSC", "espaco", "apagar", "conectar", "", "", "", "", "", "" },
};

static int teclas_da_linha(int linha)
{
    return linha == 4 ? 4 : 10;
}

static void desenhar_teclado(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    int caixa_w = l.compacto ? c->w - 60 : 620;
    int caixa_h = l.compacto ? c->h - 40 : 300;
    Rect caixa = { (c->w - caixa_w) / 2, (c->h - caixa_h) / 2, caixa_w, caixa_h };
    int x, y;
    char buf[128];

    gfx_fill_round(c, caixa, l.raio, COR_PAINEL_2);
    gfx_stroke_round(c, caixa, l.raio, COR_BORDA, 2);

    snprintf(buf, sizeof(buf), "Senha de %s", u->ssid_pendente);
    texto(c, u->f_texto, caixa.x + 16, caixa.y + 26, COR_TEXTO, buf);

    /* Campo: mostra asteriscos (é uma senha). */
    {
        Rect campo = { caixa.x + 16, caixa.y + 36, caixa.w - 32, 26 };
        char oculto[sizeof(u->entrada)];

        gfx_fill_round(c, campo, 6, wm_rgba(0, 0, 0, 120));
        gfx_stroke_round(c, campo, 6, COR_BORDA, 1);
        snprintf(oculto, sizeof(oculto), "%s", u->entrada);
        for (char *p = oculto; *p; p++) {
            *p = '*';
        }
        texto_limitado(c, u->f_texto, campo.x + 8, campo.y + 18, COR_TEXTO, oculto,
                       campo.w - 16);
    }

    x = caixa.x + 16;
    y = caixa.y + 76;
    {
        int i = 0;
        for (int linha = 0; linha < 5; linha++) {
            int n = teclas_da_linha(linha);
            int largura = (caixa.w - 32 - (n - 1) * 5) / (linha == 4 ? 4 : 10);
            x = caixa.x + 16;
            for (int col = 0; col < n; col++) {
                Rect r = { x, y, largura, l.compacto ? 28 : 34 };
                bool sel = (u->cursor_tecla == i);

                gfx_fill_round(c, r, 6, sel ? COR_ACENTO : wm_rgba(255, 255, 255, 20));
                if (sel) {
                    gfx_stroke_round(c, r, 6, COR_TEXTO, 1);
                }
                {
                    char rotulo_buf[16];
                    const char *rotulo = TECLAS[linha][col];
                    int w;

                    if (u->maiusculas && rotulo[0] >= 'a' && rotulo[0] <= 'z'
                        && rotulo[1] == '\0') {
                        snprintf(rotulo_buf, sizeof(rotulo_buf), "%c",
                                 rotulo[0] - 'a' + 'A');
                        rotulo = rotulo_buf;
                    }
                    w = largura_texto(u->f_pequeno, rotulo);
                    if (w > r.w - 8) {
                        rotulo = rotulo[0] == 'M' ? "ABC" : "...";
                        w = largura_texto(u->f_pequeno, rotulo);
                    }
                    texto(c, u->f_pequeno, r.x + (r.w - w) / 2, r.y + r.h / 2 + 5,
                          COR_TEXTO, rotulo);
                }
                x += largura + 5;
                i++;
            }
            y += (l.compacto ? 28 : 34) + 5;
        }
    }
    texto(c, u->f_pequeno, caixa.x + 16, caixa.y + caixa.h - 10, COR_MUTED,
          "A escolhe   B cancela   L/R muda de linha");
}

/* ------------------------------------------------------------- desenho --- */

void ui_render(Ui *u)
{
    Canvas *c = u->canvas;
    Layout l = layout_de(c);

    if (u->video_tela_cheia && u->aba == ABA_VIDEOS) {
        desenhar_video_tela_cheia(u);
        if (u->teclado_aberto) {
            desenhar_teclado(u, l);
        }
        return;
    }

    pintar_fundo(u);
    desenhar_topo(u, l);

    {
        Rect area = { l.margem, l.topo_h + l.gap,
                      c->w - 2 * l.margem, c->h - l.topo_h - l.rodape_h - 2 * l.gap };

        switch (u->aba) {
        case ABA_MUSICA: {
            int col_esq = area.w * 42 / 100;

            Rect painel = { area.x, area.y, col_esq,
                            area.h - (l.compacto ? 46 : 56) - l.gap };
            Rect controles = { area.x, area.y + painel.h + l.gap,
                               col_esq, l.compacto ? 46 : 56 };
            Rect lista = { area.x + col_esq + l.gap, area.y,
                           area.w - col_esq - l.gap, area.h };
            char buf[64];

            desenhar_painel_musica(u, l, painel);
            gfx_fill_round(c, controles, l.raio, COR_PAINEL);
            gfx_stroke_round(c, controles, l.raio, wm_rgba(255, 255, 255, 18), 1);
            desenhar_controles(u, l, controles, true);

            gfx_fill_round(c, lista, l.raio, COR_PAINEL);
            gfx_stroke_round(c, lista, l.raio, wm_rgba(255, 255, 255, 18), 1);
            snprintf(buf, sizeof(buf), "Biblioteca · %d faixa(s)%s", u->n_musicas,
                     library_scanning(u->lib) ? " (varrendo)" : "");
            texto(c, u->f_pequeno, lista.x + 14, lista.y + 22, COR_MUTED, buf);

            {
                int y = lista.y + 34;
                int linhas = (lista.h - 44) / (l.altura_linha - 6);
                int inicio = u->topo_lista;

                if (u->cursor_tecla < inicio) {
                    inicio = u->cursor_tecla;
                }
                if (u->cursor_tecla >= inicio + linhas) {
                    inicio = u->cursor_tecla - linhas + 1;
                }
                for (int i = 0; i < linhas; i++) {
                    int posicao = inicio + i;
                    struct MediaItem item;
                    Rect r;
                    bool sel;

                    if (!item_da_aba(u, posicao, &item)) {
                        break;
                    }
                    r = (Rect) { lista.x + 8, y, lista.w - 16, l.altura_linha - 6 };
                    sel = (u->foco == FOCO_LISTA && posicao == u->cursor_tecla);
                    if (sel) {
                        gfx_fill_round(c, r, 8, wm_rgba(244, 63, 94, 45));
                        gfx_stroke_round(c, r, 8, COR_ACENTO, 1);
                    }
                    {
                        char titulo[256];
                        if (item.artista[0]) {
                            snprintf(titulo, sizeof(titulo), "%.150s · %.90s",
                                     item.titulo, item.artista);
                        } else {
                            snprintf(titulo, sizeof(titulo), "%.240s", item.titulo);
                        }
                        texto_limitado(c, u->f_pequeno, r.x + 10, r.y + r.h / 2 + 5,
                                       COR_TEXTO, titulo, r.w - 30);
                    }
                    y += l.altura_linha - 6;
                }
                if (u->n_musicas == 0) {
                    texto(c, u->f_pequeno, lista.x + 14, lista.y + 56, COR_MUTED,
                          "Nada encontrado. Copie músicas para /storage/media.");
                }
            }
            break;
        }
        case ABA_VIDEOS:
            desenhar_grade_videos(u, l, area);
            break;
        case ABA_RADIO:
            desenhar_radio(u, l);
            break;
        default:
            desenhar_sistema(u, l);
            break;
        }
    }

    desenhar_rodape(u, l);

    if (u->teclado_aberto) {
        desenhar_teclado(u, l);
    }
}

/* --------------------------------------------------------------- ações --- */

static void pedir_tocar(Ui *u)
{
    struct MediaItem item;

    if (u->foco != FOCO_LISTA) {
        u->quer_toggle = true;
        return;
    }
    if (!item_da_aba(u, u->cursor_tecla, &item)) {
        return;
    }
    snprintf(u->quer_tocar, sizeof(u->quer_tocar), "%s", item.caminho);
    u->tem_quer_tocar = true;
    u->quer_seek = 0;
    u->tem_quer_seek = true;
    u->osd_ate = wm_now() + 3.0;
    /* Vídeo abre em tela cheia; áudio (ou vídeo já tocando) usa a UI normal.
     * Sem o "else" a tela cheia ficaria preta ao trocar de um vídeo para música. */
    u->video_tela_cheia = (item.tipo == WM_MIDIA_VIDEO);
}

static void abrir_teclado(Ui *u, const char *ssid)
{
    u->teclado_aberto = true;
    u->entrada[0] = '\0';
    u->cursor_tecla = 0;
    snprintf(u->ssid_pendente, sizeof(u->ssid_pendente), "%.63s", ssid ? ssid : "");
}

static void usar_tecla(Ui *u, int idx)
{
    int linha = 0;
    int resto = idx;

    while (linha < 5 && resto >= teclas_da_linha(linha)) {
        resto -= teclas_da_linha(linha);
        linha++;
    }
    if (linha >= 5) {
        return;
    }
    {
        const char *t = TECLAS[linha][resto];

        if (strcmp(t, "apagar") == 0) {
            size_t n = strlen(u->entrada);
            if (n > 0) {
                u->entrada[n - 1] = '\0';
            }
        } else if (strcmp(t, "espaco") == 0) {
            size_t n = strlen(u->entrada);
            if (n + 1 < sizeof(u->entrada)) {
                u->entrada[n] = ' ';
                u->entrada[n + 1] = '\0';
            }
        } else if (strcmp(t, "conectar") == 0) {
            radio_conectar_wifi(u->radio, u->ssid_pendente, u->entrada);
            u->teclado_aberto = false;
        } else if (strcmp(t, "MAIUSC") == 0) {
            u->maiusculas = !u->maiusculas;
        } else {
            size_t n = strlen(u->entrada);
            char c = t[0];

            if (u->maiusculas && c >= 'a' && c <= 'z') {
                c = (char) (c - 'a' + 'A');
            }
            if (n + 1 < sizeof(u->entrada)) {
                u->entrada[n] = c;
                u->entrada[n + 1] = '\0';
            }
        }
    }
}

static int total_teclas(void)
{
    int total = 0;
    for (int i = 0; i < 5; i++) {
        total += teclas_da_linha(i);
    }
    return total;
}

int ui_handle_key(Ui *u, const struct KeyEvent *ev)
{
    int acao = WM_ACAO_NENHUMA;
    int n = itens_da_aba(u);

    if (ev->soltou) {
        return WM_ACAO_NENHUMA;
    }

    if (u->teclado_aberto) {
        int total = total_teclas();
        switch (ev->key) {
        case WM_KEY_ESQ:
            u->cursor_tecla = (u->cursor_tecla - 1 + total) % total;
            break;
        case WM_KEY_DIR:
            u->cursor_tecla = (u->cursor_tecla + 1) % total;
            break;
        case WM_KEY_CIMA:
            u->cursor_tecla = (u->cursor_tecla - 10 + total) % total;
            break;
        case WM_KEY_BAIXO:
            u->cursor_tecla = (u->cursor_tecla + 10) % total;
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            usar_tecla(u, u->cursor_tecla);
            break;
        case WM_KEY_VOLTAR:
            u->teclado_aberto = false;
            break;
        default:
            break;
        }
        return WM_ACAO_NENHUMA;
    }

    if (u->video_tela_cheia) {
        switch (ev->key) {
        case WM_KEY_VOLTAR:
        case WM_KEY_ABA_ANT:
        case WM_KEY_ABA_PROX:
            u->video_tela_cheia = false;
            u->osd_ate = 0;
            break;
        case WM_KEY_TOCAR_PAUSAR:
            u->quer_toggle = true;
            u->osd_ate = wm_now() + 3.0;
            break;
        case WM_KEY_ESQ:
            u->quer_seek = -10;
            u->tem_quer_seek = true;
            u->osd_ate = wm_now() + 3.0;
            break;
        case WM_KEY_DIR:
            u->quer_seek = 10;
            u->tem_quer_seek = true;
            u->osd_ate = wm_now() + 3.0;
            break;
        case WM_KEY_VOL_MAIS:
            u->quer_volume = audio_volume(u->audio) + 5;
            u->tem_quer_volume = true;
            break;
        case WM_KEY_VOL_MENOS:
            u->quer_volume = audio_volume(u->audio) - 5;
            u->tem_quer_volume = true;
            break;
        default:
            break;
        }
        return WM_ACAO_NENHUMA;
    }

    switch (ev->key) {
    case WM_KEY_ABA_PROX:
        u->aba = (u->aba + 1) % TELAS;
        u->foco = FOCO_LISTA;
        u->cursor_tecla = 0;
        u->topo_lista = 0;
        break;
    case WM_KEY_ABA_ANT:
        u->aba = (u->aba - 1 + TELAS) % TELAS;
        u->foco = FOCO_LISTA;
        u->cursor_tecla = 0;
        u->topo_lista = 0;
        break;
    case WM_KEY_VOLTAR:
        acao = WM_ACAO_SAIR;
        break;
    case WM_KEY_TOCAR_PAUSAR:
        u->quer_toggle = true;
        break;
    case WM_KEY_VOL_MAIS:
        u->quer_volume = audio_volume(u->audio) + 5;
        u->tem_quer_volume = true;
        break;
    case WM_KEY_VOL_MENOS:
        u->quer_volume = audio_volume(u->audio) - 5;
        u->tem_quer_volume = true;
        break;
    default:
        break;
    }
    if (acao != WM_ACAO_NENHUMA) {
        return acao;
    }

    switch (u->aba) {
    case ABA_MUSICA:
    case ABA_VIDEOS: {
        int colunas = (u->aba == ABA_VIDEOS) ? (layout_de(u->canvas).compacto ? 2 : 3) : 1;

        switch (ev->key) {
        case WM_KEY_CIMA:
            if (u->foco == FOCO_CONTROLES) {
                u->foco = FOCO_LISTA;
            } else if (u->cursor_tecla >= colunas) {
                u->cursor_tecla -= colunas;
            }
            break;
        case WM_KEY_BAIXO:
            if (u->foco == FOCO_LISTA && n <= 0) {
                u->foco = FOCO_CONTROLES;
            } else if (u->cursor_tecla + colunas < n) {
                u->cursor_tecla += colunas;
            } else if (u->cursor_tecla + 1 < n && colunas == 3) {
                u->cursor_tecla = n - 1;
            } else if (u->aba == ABA_MUSICA) {
                u->foco = FOCO_CONTROLES;
            }
            if (u->cursor_tecla >= n) {
                u->cursor_tecla = n > 0 ? n - 1 : 0;
            }
            break;
        case WM_KEY_ESQ:
            if (u->foco == FOCO_CONTROLES) {
                if (u->foco_botao > 0) {
                    u->foco_botao--;
                }
            } else if (colunas > 1 && u->cursor_tecla > 0) {
                u->cursor_tecla--;
            }
            break;
        case WM_KEY_DIR:
            if (u->foco == FOCO_CONTROLES) {
                if (u->foco_botao < 3) {
                    u->foco_botao++;
                }
            } else if (colunas > 1 && u->cursor_tecla + 1 < n) {
                u->cursor_tecla++;
            }
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            if (u->foco == FOCO_CONTROLES) {
                /* 0 = anterior, 1 = play/pause, 2 = próximo, 3 = parar */
                switch (u->foco_botao) {
                case 0:
                    u->quer_toggle = true;   /* anterior é tratado no main */
                    u->quer_anterior = true;
                    break;
                case 1:
                    u->quer_toggle = true;
                    break;
                case 2:
                    u->quer_proximo = true;
                    break;
                default:
                    u->quer_parar = true;
                    break;
                }
            } else {
                pedir_tocar(u);
            }
            break;
        case WM_KEY_PG_BAIXO:
            u->cursor_tecla += colunas * 4;
            if (u->cursor_tecla >= n) {
                u->cursor_tecla = n > 0 ? n - 1 : 0;
            }
            break;
        case WM_KEY_PG_CIMA:
            u->cursor_tecla -= colunas * 4;
            if (u->cursor_tecla < 0) {
                u->cursor_tecla = 0;
            }
            break;
        default:
            break;
        }
        break;
    }
    case ABA_RADIO:
        switch (ev->key) {
        case WM_KEY_CIMA:
            if (u->cursor_tecla > 0) {
                u->cursor_tecla--;
            }
            break;
        case WM_KEY_BAIXO:
            if (u->cursor_tecla + 1 < u->n_redes) {
                u->cursor_tecla++;
            }
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            if (u->cursor_tecla < u->n_redes) {
                struct Rede *rede = &u->redes[u->cursor_tecla];
                if (rede->seguranca[0] && wm_casecmp(rede->seguranca, "aberta") != 0) {
                    abrir_teclado(u, rede->ssid);
                } else {
                    radio_conectar_wifi(u->radio, rede->ssid, "");
                }
            }
            break;
        case WM_KEY_MENU:
            radio_scan_wifi(u->radio);
            break;
        case WM_KEY_PG_BAIXO:
            radio_bt_scan(u->radio);
            break;
        default:
            break;
        }
        break;
    case ABA_SISTEMA:
        switch (ev->key) {
        case WM_KEY_ESQ:
        case WM_KEY_VOL_MENOS:
            u->quer_brilho = u->sys.brilho > 0 ? u->sys.brilho - 5 : -1;
            acao = WM_ACAO_BRILHO;
            break;
        case WM_KEY_DIR:
        case WM_KEY_VOL_MAIS:
            u->quer_brilho = u->sys.brilho >= 0 ? u->sys.brilho + 5 : -1;
            if (u->quer_brilho > 100) {
                u->quer_brilho = 100;
            }
            acao = WM_ACAO_BRILHO;
            break;
        case WM_KEY_MENU:
            acao = WM_ACAO_DESLIGAR;
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            acao = WM_ACAO_REINICIAR;
            break;
        case WM_KEY_PG_BAIXO:
            acao = WM_ACAO_RESSCAN;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return acao;
}

/* ------------------------------------------------------------- ligação --- */

Ui *ui_create(Canvas *canvas, Library *lib, AudioOut *audio)
{
    Ui *u = calloc(1, sizeof(*u));
    static const char *caminhos_fonte[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL
    };
    const char *fonte = NULL;
    bool compacto;

    if (!u) {
        return NULL;
    }
    u->canvas = canvas;
    u->lib = lib;
    u->audio = audio;
    compacto = canvas->w < 900;

    for (int i = 0; caminhos_fonte[i]; i++) {
        if (access(caminhos_fonte[i], R_OK) == 0) {
            fonte = caminhos_fonte[i];
            break;
        }
    }
    if (!fonte) {
        wm_log("!!! ui: nenhuma fonte DejaVu encontrada; o texto nao vai aparecer");
    } else {
        u->f_titulo = font_open(fonte, compacto ? 19 : 27);
        u->f_grande = font_open(fonte, compacto ? 16 : 21);
        u->f_texto = font_open(fonte, compacto ? 13 : 16);
        u->f_pequeno = font_open(fonte, compacto ? 11 : 13);
        u->f_numero = font_open(fonte, compacto ? 11 : 13);
    }
    u->n_redes = 0;
    return u;
}

void ui_destroy(Ui *u)
{
    if (!u) {
        return;
    }
    font_close(u->f_titulo);
    font_close(u->f_grande);
    font_close(u->f_texto);
    font_close(u->f_pequeno);
    font_close(u->f_numero);
    free(u->capa);
    free(u);
}

void ui_set_player(Ui *u, Player *p)
{
    if (u) {
        u->player = p;
    }
}

void ui_update_system(Ui *u, const struct SystemInfo *sys)
{
    if (!u || !sys) {
        return;
    }
    u->sys = *sys;
    u->n_redes = radio_redes(u->radio, u->redes, 32);
    /* As listas só mudam quando uma varredura termina: comparar a versão evita
     * percorrer a biblioteca a cada segundo. */
    {
        unsigned versao = library_version(u->lib);
        if (versao != u->versao_biblioteca) {
            montar_listas(u);
            u->versao_biblioteca = versao;
        }
    }
}

const char *ui_want_play(Ui *u, double *seek_to)
{
    if (!u || !u->tem_quer_tocar) {
        return NULL;
    }
    u->tem_quer_tocar = false;
    snprintf(u->tocando_path, sizeof(u->tocando_path), "%s", u->quer_tocar);
    if (seek_to) {
        *seek_to = u->tem_quer_seek ? u->quer_seek : 0;
    }
    u->tem_quer_seek = false;
    return u->quer_tocar;
}

int ui_want_volume(Ui *u)
{
    int v;

    if (!u || !u->tem_quer_volume) {
        return -1;
    }
    v = u->quer_volume;
    u->tem_quer_volume = false;
    u->quer_volume = 0;
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    return v;
}

int ui_want_brightness(Ui *u)
{
    int v;

    if (!u || u->quer_brilho == 0) {
        return -1;
    }
    v = u->quer_brilho;
    u->quer_brilho = 0;
    return v;
}

/* Pedidos de transporte da barra de controles ("anterior/próximo/parar"). */
int ui_want_transport(Ui *u)
{
    int acao = 0;

    if (!u) {
        return 0;
    }
    if (u->quer_anterior) {
        u->quer_anterior = false;
        acao = 1;
    } else if (u->quer_proximo) {
        u->quer_proximo = false;
        acao = 2;
    } else if (u->quer_parar) {
        u->quer_parar = false;
        acao = 3;
    }
    return acao;
}

bool ui_wants_toggle(Ui *u)
{
    if (!u || !u->quer_toggle) {
        return false;
    }
    u->quer_toggle = false;
    return true;
}

double ui_want_seek(Ui *u, bool *absoluto)
{
    if (!u || !u->tem_quer_seek) {
        return 0;
    }
    u->tem_quer_seek = false;
    if (absoluto) {
        *absoluto = false;
    }
    return u->quer_seek;
}

void ui_set_tab(Ui *u, int aba)
{
    if (u && aba >= 0 && aba < TELAS) {
        u->aba = aba;
        u->cursor_tecla = 0;
        u->foco = FOCO_LISTA;
    }
}

void ui_set_video_fullscreen(Ui *u, bool ativo)
{
    if (u) {
        u->video_tela_cheia = ativo;
    }
}

void ui_set_current_path(Ui *u, const char *caminho)
{
    if (!u) {
        return;
    }
    if (caminho && *caminho) {
        snprintf(u->tocando_path, sizeof(u->tocando_path), "%.*s",
                 (int) sizeof(u->tocando_path) - 1, caminho);
    } else {
        u->tocando_path[0] = '\0';
    }
}

/* Faixa anterior/próxima: anda na lista da aba atual a partir do que está
 * tocando (com volta ao começo). Devolve o caminho a abrir, ou NULL. */
const char *ui_step_track(Ui *u, int delta)
{
    static char escolhido[WM_PATH_MAX];
    struct MediaItem item;
    int atual = -1;
    int n = itens_da_aba(u);

    if (!u || n <= 0) {
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        if (item_da_aba(u, i, &item) && u->tocando_path[0]
            && strcmp(item.caminho, u->tocando_path) == 0) {
            atual = i;
            break;
        }
    }
    if (atual < 0) {
        atual = u->cursor_tecla;
    }
    atual = (atual + delta % n + n) % n;
    if (!item_da_aba(u, atual, &item)) {
        return NULL;
    }
    snprintf(escolhido, sizeof(escolhido), "%s", item.caminho);
    u->cursor_tecla = atual;
    return escolhido;
}

void ui_show_osd(Ui *u, double segundos)
{
    if (u) {
        u->osd_ate = wm_now() + segundos;
    }
}

void ui_set_radio(Ui *u, Radio *r)
{
    if (u) {
        u->radio = r;
    }
}

void ui_refresh_library(Ui *u)
{
    if (u) {
        montar_listas(u);
        u->versao_biblioteca = library_version(u->lib);
    }
}

bool ui_video_fullscreen(const Ui *u)
{
    return u && u->video_tela_cheia;
}
