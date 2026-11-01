/* ui.c — telas, navegação e desenho da UI.
 *
 * Visual: os cinco temas de variants/media-player/layout (layout1..5.md). Nada
 * de cor fixa aqui: tudo sai de tema_atual(), então trocar de tema no menu muda
 * a tela inteira. Tudo é desenhado à mão no canvas, sem toolkit: assim o app
 * continua sendo um binário só.
 *
 * Abas (4, com L/R para trocar):
 *   Música     — capa, título/artista/álbum, progresso, controles e a lista
 *   Vídeos     — grade da biblioteca + reprodução em tela cheia
 *   Biblioteca — navegação por ÁLBUM: lista de álbuns -> faixas do álbum
 *   Sistema    — SÓ informação: modelo, bateria, armazenamento, uptime
 *
 * Nada se configura na aba Sistema (o botão de volume do hardware se confundia
 * com o brilho ali). Todo ajuste mora no menu, aberto com SELECT em qualquer
 * aba. O menu tem submenus, e é lá que ficam Wi-Fi, Bluetooth, som (equalizador
 * de 10 bandas), idioma (10), relógio e tema — como o usuário pediu.
 *
 * Entrada: só o gamepad/teclado (não há toque). A navegação é vertical dentro
 * da aba e horizontal entre abas, com as teclas L/R para pular de aba, L1/R1
 * para saltar faixa/página e o botão de power para apagar/acender a tela.
 *
 * Todos os textos passam por T(MSG_*), de i18n.c. A fonte primária é DejaVu,
 * com reservas Noto (árabe, devanágari, bengali) e WenQuanYi (chinês): o
 * shaping fica no font.c, com HarfBuzz.
 */

#include "wm.h"

#include <dirent.h>
#include <math.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define ITENS_LISTA_MAX 400
#define ALBUNS_MAX 200
#define BT_DEV_MAX 16
#define TELAS 4

/* A aba "Rádio" saiu: estes aparelhos não têm AM/FM. Wi-Fi e Bluetooth passaram
 * para o menu, e o lugar dela na barra virou a Biblioteca. */
enum { ABA_MUSICA = 0, ABA_VIDEOS = 1, ABA_BIBLIOTECA = 2, ABA_SISTEMA = 3 };

enum {
    FOCO_LISTA = 0,
    FOCO_CONTROLES = 1
};

/* Piso de brilho. Abaixo disso o painel fica ilegível e, pior, parece que o
 * aparelho travou. Para economizar bateria existe "Apagar a tela" (DPMS). */
#define BRILHO_MIN WM_BRILHO_MIN

/* Índice dos itens do menu de configurações. */
enum {
    MENU_BRILHO = 0,
    MENU_WIFI,
    MENU_BT,
    MENU_SOM,
    MENU_IDIOMA,
    MENU_RELOGIO,
    MENU_TEMA,
    MENU_APAGAR,
    MENU_DESLIGAR,
    MENU_REINICIAR,
    MENU_FECHAR,
    MENU_ITENS
};

/* Tela atual do menu. RAIZ é a lista de cima; os outros são os submenus. */
enum {
    TELA_RAIZ = 0,
    TELA_WIFI,
    TELA_BT,
    TELA_SOM,
    TELA_EQ,
    TELA_IDIOMA,
    TELA_RELOGIO,
    TELA_TEMA,
    TELA_TOTAL
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
    struct BtDevice bt_devs[BT_DEV_MAX];
    int n_bt_devs;

    /* Biblioteca por álbum: u->musicas[] fica ordenado por álbum, então cada
     * álbum é um trecho contíguo (ini/n) dessa mesma lista. O álbum 0 é
     * sintético: "Todas as músicas". */
    int alb_ini[ALBUNS_MAX];
    int alb_n[ALBUNS_MAX];
    int alb_lib[ALBUNS_MAX];    /* índice na library do 1º item do álbum */
    int n_albuns;
    int bib_album;              /* álbum em foco na aba Biblioteca */
    int bib_faixa;              /* faixa em foco dentro do álbum aberto */
    bool bib_aberto;            /* entrou no álbum (mostra as faixas) */
    int bib_topo;
    int bib_topo_faixas;

    /* Modo de reprodução (repetir faixa/álbum, aleatório). */
    int modo;

    bool video_tela_cheia;
    bool mostrar_ajuda;
    double osd_ate;

    char botoes[6][64];   /* 5 botões (anterior, play, próximo, parar, modo) */
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
    bool tem_quer_brilho;
    bool quer_anterior;
    bool quer_proximo;
    bool quer_parar;

    /* teclado na tela (senha do Wi-Fi) */
    bool teclado_aberto;
    bool maiusculas;
    char entrada[128];

    /* Menu de configurações (aberto pelo SELECT) e diálogo de confirmação.
     * A aba Sistema ficou só de informação: qualquer ajuste acontece aqui. */
    bool menu_aberto;
    int menu_item;
    int menu_tela;          /* TELA_* */
    int menu_curso;         /* cursor dentro do submenu */
    int menu_top[TELA_TOTAL]; /* cursor de cada submenu, para voltar onde estava */
    bool dialogo_aberto;
    int dialogo_item;
    bool dialogo_reiniciar; /* o diálogo também serve para reiniciar */
    bool tela_apagada;
    int cursor_tecla;
    char ssid_pendente[64];

    /* Relógio do menu: valores em edição (só valem ao aplicar). */
    struct tm relogio;
    int relogio_item;

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

/* Todas as cores da UI vêm do tema ativo (tema.c). Antes eram constantes aqui e
 * trocar de tema exigiria recompilar; com macro, cada uso continua igual e o
 * valor é lido na hora do desenho. */
#define COR_FUNDO (tema_atual()->fundo)
#define COR_FUNDO_2 (tema_atual()->fundo2)
#define COR_PAINEL (tema_atual()->painel)
#define COR_PAINEL_2 (tema_atual()->painel2)
#define COR_BORDA (tema_atual()->borda)
#define COR_ACENTO (tema_atual()->acento)
#define COR_ACENTO_ESC (tema_atual()->acento_esc)
#define COR_TEXTO (tema_atual()->texto)
#define COR_MUTED (tema_atual()->muted)
#define COR_BARRA (tema_atual()->barra)

/* Verde de "tudo certo" não faz parte da paleta de nenhum tema: é semântico
 * (Wi-Fi conectado, bateria carregando) e por isso fica fixo. */
static const Color COR_OK = { 52, 211, 153, 255 };

/* Acento com transparência — usado nos realces das listas. */
static Color acento_alfa(int a)
{
    Color c = tema_atual()->acento;

    c.a = (uint8_t) a;
    return c;
}

/* Texto legível sobre uma cor cheia: os temas claros (mono, vidro) têm acento
 * branco, e texto branco em botão branco desapareceria. */
static Color cor_contraste(Color fundo)
{
    int lum = (299 * fundo.r + 587 * fundo.g + 114 * fundo.b) / 1000;

    return lum > 150 ? wm_rgb(12, 12, 14) : wm_rgb(250, 250, 252);
}

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

/* Monta musicas[]/videos[] e agrupa as músicas por álbum.
 *
 * A ordenação é por inserção sobre arrays auxiliares: a lista tem no máximo 400
 * itens e isso roda uma vez por varredura, então não vale um qsort com
 * comparador que precisaria chamar library_item() em cada comparação. */
typedef struct {
    int lib;
    char album[96];
    char titulo[192];
} ItemAux;

static int cmp_aux(const ItemAux *a, const ItemAux *b)
{
    int c = wm_casecmp(a->album, b->album);

    if (c != 0) {
        return c;
    }
    return wm_casecmp(a->titulo, b->titulo);
}

static void montar_listas(Ui *u)
{
    int total = library_count(u->lib);
    struct MediaItem item;
    ItemAux *aux = NULL;
    int n_aux = 0;

    u->n_musicas = 0;
    u->n_videos = 0;
    u->n_albuns = 0;
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
    if (u->n_musicas == 0) {
        u->bib_album = 0;
        u->bib_aberto = false;
        return;
    }

    aux = calloc((size_t) u->n_musicas, sizeof(*aux));
    if (!aux) {
        return;
    }
    for (int i = 0; i < u->n_musicas; i++) {
        if (!library_item(u->lib, u->musicas[i], &item)) {
            continue;
        }
        aux[n_aux].lib = u->musicas[i];
        snprintf(aux[n_aux].album, sizeof(aux[n_aux].album), "%.95s",
                 item.album[0] ? item.album : T(MSG_SEM_ALBUM));
        snprintf(aux[n_aux].titulo, sizeof(aux[n_aux].titulo), "%.191s",
                 item.titulo[0] ? item.titulo : item.caminho);
        n_aux++;
    }
    /* Ordenação por inserção: O(n²) com n <= 400 = 160 mil comparações de
     * string no pior caso, e o caso normal (já quase ordenado) é O(n). */
    for (int i = 1; i < n_aux; i++) {
        ItemAux chave = aux[i];
        int j = i - 1;

        while (j >= 0 && cmp_aux(&aux[j], &chave) > 0) {
            aux[j + 1] = aux[j];
            j--;
        }
        aux[j + 1] = chave;
    }

    for (int i = 0; i < n_aux; i++) {
        u->musicas[i] = aux[i].lib;
    }

    /* Álbum 0 é sintético: "todas as músicas", para tocar tudo em ordem. */
    u->alb_ini[0] = 0;
    u->alb_n[0] = n_aux;
    u->alb_lib[0] = aux[0].lib;
    u->n_albuns = 1;

    for (int i = 0; i < n_aux && u->n_albuns < ALBUNS_MAX;) {
        int j = i;

        while (j + 1 < n_aux && strcmp(aux[j + 1].album, aux[i].album) == 0) {
            j++;
        }
        u->alb_ini[u->n_albuns] = i;
        u->alb_n[u->n_albuns] = j - i + 1;
        u->alb_lib[u->n_albuns] = aux[i].lib;
        u->n_albuns++;
        i = j + 1;
    }
    free(aux);

    if (u->bib_album >= u->n_albuns) {
        u->bib_album = 0;
        u->bib_aberto = false;
    }
    if (u->bib_faixa >= u->alb_n[u->bib_album]) {
        u->bib_faixa = 0;
    }
}

/* Quantos itens o cursor da aba percorre. Na Biblioteca fechada o cursor anda
 * por ÁLBUNS (não por faixas) — é o que dá a navegação por álbum. */
static int itens_da_aba(const Ui *u)
{
    switch (u->aba) {
    case ABA_MUSICA:
        return u->n_musicas;
    case ABA_VIDEOS:
        return u->n_videos;
    case ABA_BIBLIOTECA:
        return u->bib_aberto ? u->alb_n[u->bib_album] : u->n_albuns;
    default:
        return 0;
    }
}

/* Quantas faixas a lista de faixas da aba tem (para anterior/próxima). */
static int faixas_da_aba(const Ui *u)
{
    if (u->aba == ABA_BIBLIOTECA) {
        return u->bib_aberto ? u->alb_n[u->bib_album] : u->n_musicas;
    }
    return itens_da_aba(u);
}

/* Item de mídia na posição `posicao` da lista de faixas da aba. */
static bool faixa_da_aba(Ui *u, int posicao, struct MediaItem *out)
{
    int indice;

    if (posicao < 0) {
        return false;
    }
    if (u->aba == ABA_MUSICA
        || (u->aba == ABA_BIBLIOTECA && !u->bib_aberto)) {
        if (posicao >= u->n_musicas) {
            return false;
        }
        indice = u->musicas[posicao];
    } else if (u->aba == ABA_BIBLIOTECA) {
        if (posicao >= u->alb_n[u->bib_album]) {
            return false;
        }
        indice = u->musicas[u->alb_ini[u->bib_album] + posicao];
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
    const char *nomes[TELAS] = {
        T(MSG_ABA_MUSICA), T(MSG_ABA_VIDEOS), T(MSG_ABA_BIBLIOTECA), T(MSG_ABA_SISTEMA)
    };
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

        {
            /* "bt"/"wifi" são siglas: não traduzem, mas o estado sim. */
            const char *rotulo_bt = u->sys.bt_ligado ? "bt" : "bt";

            chip_x -= largura_texto(u->f_pequeno, rotulo_bt) + 20;
            chip(c, u->f_pequeno, chip_x, base - 6, rotulo_bt, COR_OK,
                 u->sys.bt_ligado);
        }
        chip_x -= l.compacto ? 4 : 8;

        chip_x -= largura_texto(u->f_pequeno, "wifi") + 20;
        snprintf(buf, sizeof(buf), "%s", "wifi");
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
        texto_limitado(c, u->f_pequeno, l.margem, y + l.rodape_h / 2 + 4, COR_MUTED,
                       T(MSG_AJUDA_ABAS), c->w / 2 - l.margem);
        snprintf(buf, sizeof(buf), "%s", st.aberto ? st.titulo : "");
        texto_limitado(c, u->f_pequeno, c->w / 2, y + l.rodape_h / 2 + 4, COR_TEXTO,
                       buf, c->w / 2 - l.margem - 40);
        snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
        texto(c, u->f_pequeno, c->w - l.margem - 34, y + l.rodape_h / 2 + 4,
              COR_MUTED, buf);
        return;
    }

    texto(c, u->f_pequeno, l.margem, y + l.rodape_h / 2 + 5, COR_MUTED,
          T(MSG_AJUDA_ABAS));

    /* Mini player centralizado à direita. */
    {
        char titulo[128];
        int x = c->w - l.margem;

        snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
        x -= largura_texto(u->f_pequeno, buf);
        texto(c, u->f_pequeno, x, y + l.rodape_h / 2 + 5, COR_MUTED, buf);
        x -= 14;

        snprintf(titulo, sizeof(titulo), "%.*s", (int) sizeof(titulo) - 1,
                 st.aberto ? st.titulo : T(MSG_NADA_TOCANDO));
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
        if (faixa_da_aba(u, i, &atual) && u->tocando_path[0]
            && strcmp(atual.caminho, u->tocando_path) == 0) {
            tem_item = true;
        }
    }
    if (!tem_item && faixa_da_aba(u, u->cursor_tecla, &atual)) {
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

        gfx_fill_round(c, (Rect) { x, y, etiqueta_w, 20 }, 10, acento_alfa(45));
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
                           acento_alfa(255));
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
                       st.aberto ? st.titulo : T(MSG_NADA_TOCANDO), max);
        y += font_line_height(u->f_titulo) + 2;
        texto_limitado(c, u->f_texto, x, y + font_ascent(u->f_texto), COR_MUTED,
                       st.aberto ? (st.artista[0] ? st.artista : T(MSG_DESCONHECIDO))
                                 : T(MSG_SEM_MUSICA),
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
/* Rótulo curto do modo de reprodução, para o botão e para o rodapé. */
static const char *rotulo_modo(int modo)
{
    switch (modo) {
    case WM_MODO_REPETIR_ALBUM:
        return T(MSG_MODO_REPETIR_TUDO);
    case WM_MODO_REPETIR_FAIXA:
        return T(MSG_MODO_REPETIR_FAIXA);
    case WM_MODO_ALEATORIO:
        return T(MSG_MODO_ALEATORIA);
    default:
        return T(MSG_MODO_SEQUENCIAL);
    }
}

static void desenhar_controles(Ui *u, Layout l, Rect area, bool com_foco)
{
    Canvas *c = u->canvas;
    /* 0 anterior · 1 play/pause · 2 próximo · 3 parar · 4 modo */
    static const char *simbolos[] = { "<<", "PLAY", ">>", "STOP", "" };
    int n = 5;
    int largura = l.compacto ? 50 : 72;
    int altura = l.compacto ? 24 : 32;
    int x = area.x + 12;
    int y = area.y + 10;
    int passo = largura + (l.compacto ? 5 : 8);
    struct PlayerStatus st;

    memset(&st, 0, sizeof(st));
    if (u->player) {
        player_status(u->player, &st);
    }

    u->n_botoes = 0;
    for (int i = 0; i < n; i++) {
        Rect r = { x + i * passo, y, largura, altura };
        bool atual = com_foco && u->foco == FOCO_CONTROLES && u->foco_botao == i;
        bool principal = (i == 1);
        const char *rotulo;
        int largura_rotulo;

        gfx_fill_round(c, r, altura / 2, principal ? COR_ACENTO : wm_rgba(255, 255, 255, 22));
        if (atual) {
            gfx_stroke_round(c, r, altura / 2, COR_TEXTO, 2);
        }
        if (i == 1) {
            rotulo = st.tocando ? T(MSG_PAUSAR) : T(MSG_TOCAR);
        } else if (i == 4) {
            /* Modo: o símbolo do modo em vez da palavra "modo". */
            rotulo = rotulo_modo(u->modo);
        } else {
            rotulo = simbolos[i];
        }
        largura_rotulo = largura_texto(u->f_pequeno, rotulo);
        texto_limitado(c, u->f_pequeno,
                       r.x + (r.w - largura_rotulo) / 2, r.y + r.h / 2 + 5,
                       principal ? cor_contraste(COR_ACENTO) : COR_MUTED,
                       rotulo, r.w - 6);
        snprintf(u->botoes[u->n_botoes++], 64, "%s", rotulo);
    }
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

    snprintf(buf, sizeof(buf), "%d %s%s", u->n_videos, T(MSG_ABA_VIDEOS),
             library_scanning(u->lib) ? "  (varrendo...)" : "");
    texto(c, u->f_pequeno, area.x + 14, area.y + 20, COR_MUTED, buf);

    if (u->n_videos == 0) {
        texto(c, u->f_texto, area.x + 14, area.y + 56, COR_MUTED,
              T(MSG_SEM_VIDEO));
        texto(c, u->f_pequeno, area.x + 14, area.y + 56 + font_line_height(u->f_texto),
              wm_rgba(148, 163, 184, 200),
              T(MSG_VARRENDO));
        return;
    }

    for (int i = 0; i < linhas_visiveis * colunas; i++) {
        int posicao = primeiro + i;
        int col = i % colunas;
        int lin = i / colunas;
        struct MediaItem item;
        Rect r;
        bool selecionado;

        if (!faixa_da_aba(u, posicao, &item)) {
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
              T(MSG_AJUDA_VIDEO));
    }
}

/* ---------------------------------------------------------- biblioteca --- */

/* Nome e artista de um álbum saem do primeiro item (todos os itens do trecho
 * compartilham o mesmo campo album, já que a lista está ordenada por ele). */
static bool album_info(Ui *u, int album, char *nome, size_t nome_size,
                       char *artista, size_t artista_size, int *faixas)
{
    struct MediaItem item;

    if (album < 0 || album >= u->n_albuns) {
        return false;
    }
    if (album == 0) {
        snprintf(nome, nome_size, "%s", T(MSG_TODAS_MUSICAS));
        snprintf(artista, artista_size, "%s", "");
        *faixas = u->alb_n[0];
        return true;
    }
    if (!library_item(u->lib, u->alb_lib[album], &item)) {
        return false;
    }
    snprintf(nome, nome_size, "%.*s", (int) nome_size - 1,
             item.album[0] ? item.album : T(MSG_SEM_ALBUM));
    snprintf(artista, artista_size, "%.*s", (int) artista_size - 1,
             item.artista[0] ? item.artista : T(MSG_DESCONHECIDO));
    *faixas = u->alb_n[album];
    return true;
}

/* Aba Biblioteca: álbuns à esquerda, faixas do álbum aberto à direita.
 * Pedido do usuário: "na lista da biblioteca tem que ser navegável para
 * escolher o álbum". */
static void desenhar_biblioteca(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    Rect area = { l.margem, l.topo_h + l.gap,
                  c->w - 2 * l.margem, c->h - l.topo_h - l.rodape_h - 2 * l.gap };
    Rect esq = { area.x, area.y, area.w * 46 / 100, area.h };
    Rect dir = { area.x + esq.w + l.gap, area.y, area.w - esq.w - l.gap, area.h };
    int linha_h = l.altura_linha - 4;
    int y;
    char buf[192];

    gfx_fill_round(c, esq, l.raio, COR_PAINEL);
    gfx_stroke_round(c, esq, l.raio, wm_rgba(255, 255, 255, 18), 1);
    gfx_fill_round(c, dir, l.raio, COR_PAINEL);
    gfx_stroke_round(c, dir, l.raio, wm_rgba(255, 255, 255, 18), 1);

    snprintf(buf, sizeof(buf), "%s · %d", T(MSG_ALBUNS), u->n_albuns > 0 ? u->n_albuns - 1 : 0);
    texto(c, u->f_pequeno, esq.x + 14, esq.y + 22, COR_MUTED, buf);

    if (u->n_albuns <= 1) {
        texto_limitado(c, u->f_pequeno, esq.x + 14, esq.y + 52, COR_MUTED,
                       library_scanning(u->lib) ? T(MSG_VARRENDO) : T(MSG_SEM_MUSICA),
                       esq.w - 28);
        return;
    }

    /* Lista de álbuns, com o cursor sempre visível. */
    {
        int linhas = (esq.h - 34) / linha_h;

        if (u->bib_album < u->bib_topo) {
            u->bib_topo = u->bib_album;
        }
        if (u->bib_album >= u->bib_topo + linhas) {
            u->bib_topo = u->bib_album - linhas + 1;
        }
        y = esq.y + 34;
        for (int i = 0; i < linhas; i++) {
            int album = u->bib_topo + i;
            char nome[96], artista[96];
            int faixas = 0;
            Rect r = { esq.x + 8, y, esq.w - 16, linha_h };
            bool sel = (album == u->bib_album);

            if (album >= u->n_albuns || !album_info(u, album, nome, sizeof(nome),
                                                    artista, sizeof(artista), &faixas)) {
                break;
            }
            if (sel) {
                gfx_fill_round(c, r, 8, acento_alfa(u->bib_aberto ? 28 : 45));
                gfx_stroke_round(c, r, 8, COR_ACENTO, 1);
            }
            if (album == 0) {
                texto_limitado(c, u->f_pequeno, r.x + 10, r.y + r.h / 2 + 5,
                               COR_TEXTO, nome, r.w - 60);
            } else {
                texto_limitado(c, u->f_pequeno, r.x + 10, r.y + r.h / 2 - 2,
                               sel ? COR_TEXTO : COR_TEXTO, nome, r.w - 20);
                texto_limitado(c, u->f_pequeno, r.x + 10, r.y + r.h / 2 + 12,
                               COR_MUTED, artista, r.w - 60);
            }
            snprintf(buf, sizeof(buf), "%d", faixas);
            texto(c, u->f_pequeno, r.x + r.w - largura_texto(u->f_pequeno, buf) - 8,
                  r.y + r.h / 2 + 5, COR_MUTED, buf);
            y += linha_h;
        }
    }

    /* Painel direito: capa + faixas do álbum em foco. */
    {
        char nome[96], artista[96];
        int faixas = 0;
        struct MediaItem item;
        int capa_tam = l.compacto ? 64 : 96;

        if (!album_info(u, u->bib_album, nome, sizeof(nome),
                        artista, sizeof(artista), &faixas)) {
            return;
        }
        if (u->bib_album > 0 && library_item(u->lib, u->alb_lib[u->bib_album], &item)) {
            char capa[WM_PATH_MAX];

            if (library_cover_path(&item, capa, sizeof(capa)) && carregar_capa(u, capa)) {
                Rect rc = { dir.x + 14, dir.y + 14, capa_tam, capa_tam };

                gfx_blit_round(c, rc, 8, u->capa, u->capa_w, u->capa_h);
            }
        }
        texto_limitado(c, u->f_texto, dir.x + 14 + capa_tam + 12, dir.y + 30,
                       COR_TEXTO, nome, dir.w - capa_tam - 40);
        texto_limitado(c, u->f_pequeno, dir.x + 14 + capa_tam + 12, dir.y + 50,
                       COR_MUTED, artista, dir.w - capa_tam - 40);
        snprintf(buf, sizeof(buf), "%d %s", faixas, T(MSG_FAIXAS));
        texto(c, u->f_pequeno, dir.x + 14 + capa_tam + 12, dir.y + 68, COR_MUTED, buf);

        y = dir.y + capa_tam + 24;
        {
            int linhas = (dir.y + dir.h - 30 - y) / linha_h;

            if (u->bib_faixa < u->bib_topo_faixas) {
                u->bib_topo_faixas = u->bib_faixa;
            }
            if (u->bib_faixa >= u->bib_topo_faixas + linhas) {
                u->bib_topo_faixas = u->bib_faixa - linhas + 1;
            }
            if (u->bib_topo_faixas < 0) {
                u->bib_topo_faixas = 0;
            }
            for (int i = 0; i < linhas; i++) {
                int pos = u->bib_topo_faixas + i;
                Rect r = { dir.x + 8, y, dir.w - 16, linha_h };
                bool sel = u->bib_aberto && pos == u->bib_faixa;
                struct MediaItem faixa;

                if (pos >= u->alb_n[u->bib_album]) {
                    break;
                }
                if (!library_item(u->lib, u->musicas[u->alb_ini[u->bib_album] + pos],
                                  &faixa)) {
                    break;
                }
                if (sel) {
                    gfx_fill_round(c, r, 8, acento_alfa(45));
                    gfx_stroke_round(c, r, 8, COR_ACENTO, 1);
                }
                snprintf(buf, sizeof(buf), "%02d", pos + 1);
                texto(c, u->f_pequeno, r.x + 8, r.y + r.h / 2 + 5, COR_MUTED, buf);
                texto_limitado(c, u->f_pequeno, r.x + 34, r.y + r.h / 2 + 5,
                               COR_TEXTO, faixa.titulo, r.w - 40);
                y += linha_h;
            }
        }
        if (!u->bib_aberto) {
            texto(c, u->f_pequeno, dir.x + 14, dir.y + dir.h - 12, COR_MUTED,
                  T(MSG_AJUDA_BIBLIOTECA));
        }
    }
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

    texto(c, u->f_texto, esquerda.x + 14, esquerda.y + 24, COR_TEXTO, T(MSG_INFO_SISTEMA));
    y = esquerda.y + 46;
    linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_MODELO),
               u->sys.modelo[0] ? u->sys.modelo : "?");
    {
        char tempo_ligado[32];
        wm_fmt_time((double) u->sys.uptime, tempo_ligado, sizeof(tempo_ligado));
        linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_TEMPO_LIGADO), tempo_ligado);
    }
    if (u->sys.tem_bateria) {
        snprintf(buf, sizeof(buf), "%d%%%s", u->sys.bateria,
                 u->sys.carregando ? T(MSG_CARREGANDO_BAT) : "");
        linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_BATERIA), buf);
    } else {
        linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_BATERIA), T(MSG_INDISPONIVEL));
    }
    snprintf(buf, sizeof(buf), "%d%%", audio_volume(u->audio));
    linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_VOLUME), buf);
    linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_SOM),
               audio_ok(u->audio) ? "ALSA ok" : T(MSG_SEM_AUDIO));
    linha_dado(c, u->f_pequeno, esquerda, &y, T(MSG_ABA_BIBLIOTECA),
               library_scanning(u->lib) ? T(MSG_VARRENDO) : T(MSG_OK));

    /* Armazenamento com barra de uso. */
    texto(c, u->f_texto, direita.x + 14, direita.y + 24, COR_TEXTO, T(MSG_ARMAZENAMENTO));
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

    /* Brilho só como informação: os ajustes ficam no menu (SELECT). Misturar
     * as duas coisas fez o botão de volume parecer controle de brilho. */
    y = direita.y + direita.h - 74;
    texto(c, u->f_pequeno, direita.x + 14, y, COR_MUTED, T(MSG_BRILHO));
    if (u->sys.brilho >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", u->sys.brilho);
        texto(c, u->f_pequeno, direita.x + 90, y, COR_TEXTO, buf);
        barra_progresso(c, (Rect) { direita.x + 14, y + 10, direita.w - 28, 4 },
                        u->sys.brilho / 100.0, COR_ACENTO);
    } else {
        texto(c, u->f_pequeno, direita.x + 90, y, COR_MUTED, T(MSG_INDISPONIVEL));
    }
    texto(c, u->f_pequeno, direita.x + 14, y + 34, COR_MUTED,
          T(MSG_AJUDA_SISTEMA));
}

/* ---------------------------------------------------- menu e diálogo --- */

static void caixa_modal(Canvas *c, Layout l, int larg, int alt, Rect *saida)
{
    Rect caixa = { (c->w - larg) / 2, (c->h - alt) / 2, larg, alt };

    /* Véu: deixa claro que o resto está inativo. A opacidade vem do tema para
     * os temas "vidro"/"mono" não ficarem com um véu azulado fora de paleta. */
    gfx_fill(c, (Rect) { 0, 0, c->w, c->h },
             wm_rgba(0, 0, 0, tema_atual()->veu));
    gfx_fill_round(c, caixa, l.raio, COR_PAINEL_2);
    gfx_stroke_round(c, caixa, l.raio, COR_BORDA, 2);
    *saida = caixa;
}

/* Uma linha de menu: rótulo à esquerda, valor à direita. `desativado` deixa a
 * linha apagada e sem realce (é como o modo neural aparece sem NPU). */
static void linha_menu(Ui *u, Rect r, const char *rotulo, const char *valor,
                       bool sel, bool desativado)
{
    Canvas *c = u->canvas;
    Color cor_rotulo;
    Color cor_valor;

    if (desativado) {
        cor_rotulo = wm_rgba(COR_MUTED.r, COR_MUTED.g, COR_MUTED.b, 150);
        cor_valor = cor_rotulo;
    } else {
        cor_rotulo = sel ? COR_TEXTO : COR_MUTED;
        cor_valor = sel ? COR_ACENTO : COR_MUTED;
    }

    if (sel) {
        if (desativado) {
            gfx_fill_round(c, r, 8, wm_rgba(255, 255, 255, 8));
            gfx_stroke_round(c, r, 8, COR_BORDA, 1);
        } else if (tema_atual()->brilho_foco) {
            gfx_fill_round(c, r, 8, wm_rgba(24, 24, 24, 242));
            gfx_stroke_round(c, r, 8, COR_BORDA, 2);
        } else {
            gfx_fill_round(c, r, 8, acento_alfa(60));
            gfx_stroke_round(c, r, 8, COR_ACENTO, 1);
        }
    } else {
        gfx_fill_round(c, r, 8, wm_rgba(255, 255, 255, 8));
    }
    texto_limitado(c, u->f_pequeno, r.x + 12, r.y + r.h / 2 + 5, cor_rotulo, rotulo,
                   r.w - 24 - (valor && valor[0] ? 120 : 0));
    if (valor && valor[0]) {
        texto(c, u->f_pequeno,
              r.x + r.w - largura_texto(u->f_pequeno, valor) - 12,
              r.y + r.h / 2 + 5, cor_valor, valor);
    }
}

/* Rótulos e valores dos itens da raiz do menu. */
static void rotulo_menu_raiz(Ui *u, int item, const char **rotulo, char *valor,
                             size_t valor_size, bool *desativado)
{
    *desativado = false;
    valor[0] = '\0';
    switch (item) {
    case MENU_BRILHO:
        *rotulo = T(MSG_BRILHO);
        if (u->sys.brilho >= 0) {
            snprintf(valor, valor_size, "< %d%% >", u->sys.brilho);
        } else {
            snprintf(valor, valor_size, "%s", T(MSG_INDISPONIVEL));
        }
        break;
    case MENU_WIFI:
        *rotulo = T(MSG_WIFI);
        if (!u->sys.wifi_presente) {
            snprintf(valor, valor_size, "%s", T(MSG_AUSENTE));
        } else if (u->sys.wifi_conectado) {
            snprintf(valor, valor_size, "%.24s", u->sys.wifi_nome);
        } else {
            snprintf(valor, valor_size, "%s", T(MSG_DESCONECTADO));
        }
        break;
    case MENU_BT:
        *rotulo = T(MSG_BLUETOOTH);
        if (!u->sys.bt_presente) {
            snprintf(valor, valor_size, "%s", T(MSG_AUSENTE));
        } else {
            snprintf(valor, valor_size, "%s",
                     u->sys.bt_ligado ? T(MSG_LIGADO) : T(MSG_DESLIGADO));
        }
        break;
    case MENU_SOM:
        *rotulo = T(MSG_SOM);
        snprintf(valor, valor_size, "%s", T(dsp_msg_preset(dsp_preset())));
        break;
    case MENU_IDIOMA:
        *rotulo = T(MSG_IDIOMA);
        snprintf(valor, valor_size, "%.28s", wm_lang_nome(wm_lang()));
        break;
    case MENU_RELOGIO:
        *rotulo = T(MSG_RELOGIO);
        {
            time_t agora = time(NULL);
            struct tm tm;

            localtime_r(&agora, &tm);
            snprintf(valor, valor_size, "%02d:%02d", tm.tm_hour, tm.tm_min);
        }
        break;
    case MENU_TEMA:
        *rotulo = T(MSG_TEMA);
        snprintf(valor, valor_size, "%s", T(tema_msg_nome(tema_atual_id())));
        break;
    case MENU_APAGAR:
        *rotulo = T(MSG_APAGAR_TELA);
        break;
    case MENU_DESLIGAR:
        *rotulo = T(MSG_DESLIGAR);
        break;
    case MENU_REINICIAR:
        *rotulo = T(MSG_REINICIAR);
        break;
    default:
        *rotulo = T(MSG_FECHAR_MENU);
        snprintf(valor, valor_size, "B");
        break;
    }
}

/* Número de linhas e o cursor do submenu atual. */
static int submenu_linhas(Ui *u, int tela)
{
    switch (tela) {
    case TELA_WIFI:
        /* procurar + redes + voltar */
        return u->n_redes + 2;
    case TELA_BT:
        /* ligar/desligar + procurar + dispositivos + voltar */
        return u->n_bt_devs + 3;
    case TELA_SOM:
        return 6;
    case TELA_EQ:
        return WM_EQ_BANDAS + 1;
    case TELA_IDIOMA:
        return WM_LANG_TOTAL + 1;
    case TELA_RELOGIO:
        /* ano, mês, dia, hora, minuto, aplicar, voltar */
        return 7;
    case TELA_TEMA:
        return WM_TEMA_TOTAL + 1;
    default:
        return MENU_ITENS;
    }
}

static void desenhar_submenu(Ui *u, Layout l, Rect caixa, const char *titulo)
{
    Canvas *c = u->canvas;
    int n = submenu_linhas(u, u->menu_tela);
    int linha_h = l.compacto ? 24 : 30;
    int visiveis = (caixa.h - 70) / linha_h;
    int inicio = 0;
    int y = caixa.y + 44;
    char buf[96];

    texto(c, u->f_texto, caixa.x + 16, caixa.y + 26, COR_TEXTO, titulo);

    if (u->menu_curso >= visiveis) {
        inicio = u->menu_curso - visiveis + 1;
    }
    for (int i = 0; i < visiveis; i++) {
        int item = inicio + i;
        Rect r = { caixa.x + 10, y, caixa.w - 20, linha_h - 4 };
        bool sel;
        const char *rotulo = "";
        char valor[64] = "";
        bool desativado = false;

        if (item >= n) {
            break;
        }
        sel = (u->menu_curso == item);

        switch (u->menu_tela) {
        case TELA_WIFI:
            if (item == 0) {
                rotulo = T(MSG_WIFI_PROCURAR);
                if (!u->sys.wifi_presente) {
                    desativado = true;
                    snprintf(valor, sizeof(valor), "%s", T(MSG_WIFI_SEM_RADIO));
                } else if (radio_ocupado(u->radio)) {
                    snprintf(valor, sizeof(valor), "%s", T(MSG_PROCURANDO));
                }
            } else if (item <= u->n_redes) {
                struct Rede *rede = &u->redes[item - 1];

                rotulo = rede->ssid;
                snprintf(valor, sizeof(valor), "%d%% %s", rede->sinal,
                         rede->seguranca[0] ? rede->seguranca : T(MSG_WIFI_ABERTA));
            } else {
                rotulo = u->n_redes ? T(MSG_SUBMENU_VOLTAR) : T(MSG_WIFI_NENHUMA);
                if (!u->n_redes) {
                    desativado = true;
                }
                if (u->n_redes == 0 && !u->sys.wifi_presente) {
                    rotulo = T(MSG_SUBMENU_VOLTAR);
                    desativado = false;
                }
            }
            break;
        case TELA_BT:
            if (item == 0) {
                rotulo = u->sys.bt_ligado ? T(MSG_BT_DESLIGAR) : T(MSG_BT_LIGAR);
                desativado = !u->sys.bt_presente;
            } else if (item == 1) {
                rotulo = T(MSG_BT_PROCURAR);
                desativado = !u->sys.bt_presente || !u->sys.bt_ligado;
            } else if (item < u->n_bt_devs + 2) {
                struct BtDevice *d = &u->bt_devs[item - 2];

                rotulo = d->nome;
                snprintf(valor, sizeof(valor), "%s",
                         d->conectado ? T(MSG_CONECTADO)
                                      : (d->pareado ? T(MSG_BT_VISIVEL) : ""));
            } else {
                rotulo = T(MSG_SUBMENU_VOLTAR);
            }
            break;
        case TELA_SOM:
            switch (item) {
            case 0:
                rotulo = T(MSG_SOM_PRESET);
                snprintf(valor, sizeof(valor), "< %s >", T(dsp_msg_preset(dsp_preset())));
                break;
            case 1:
                rotulo = T(MSG_SOM_PREAMP);
                snprintf(valor, sizeof(valor), "< %+.0f dB >", (double) dsp_preamp());
                break;
            case 2:
                rotulo = T(MSG_SOM_EQUALIZADOR);
                snprintf(valor, sizeof(valor), ">");
                break;
            case 3:
                rotulo = T(MSG_SOM_NEURAL);
                snprintf(valor, sizeof(valor), "%s",
                         dsp_neural() ? T(MSG_ATIVADO) : T(MSG_DESATIVADO));
                desativado = !dsp_neural_disponivel();
                break;
            case 4:
                rotulo = T(MSG_SOM_RESTAURAR);
                break;
            default:
                rotulo = T(MSG_SUBMENU_VOLTAR);
                break;
            }
            break;
        case TELA_EQ:
            if (item < WM_EQ_BANDAS) {
                int f = wm_eq_freqs[item];
                float g = dsp_banda(item);

                snprintf(buf, sizeof(buf), "%d %s", f, T(MSG_HZ));
                rotulo = buf;
                (void) T(MSG_BANDA);
                snprintf(valor, sizeof(valor), "< %+.0f %s >", (double) g, T(MSG_DB));
            } else {
                rotulo = T(MSG_SUBMENU_VOLTAR);
            }
            break;
        case TELA_IDIOMA:
            if (item < WM_LANG_TOTAL) {
                rotulo = wm_lang_nome(item);
                if (item == wm_lang()) {
                    snprintf(valor, sizeof(valor), "%s", T(MSG_EM_USO));
                }
            } else {
                rotulo = T(MSG_SUBMENU_VOLTAR);
            }
            break;
        case TELA_RELOGIO:
            switch (item) {
            case 0:
                rotulo = T(MSG_RELOGIO_ANO);
                snprintf(valor, sizeof(valor), "< %d >", u->relogio.tm_year + 1900);
                break;
            case 1:
                rotulo = T(MSG_RELOGIO_MES);
                snprintf(valor, sizeof(valor), "< %02d >", u->relogio.tm_mon + 1);
                break;
            case 2:
                rotulo = T(MSG_RELOGIO_DIA);
                snprintf(valor, sizeof(valor), "< %02d >", u->relogio.tm_mday);
                break;
            case 3:
                rotulo = T(MSG_RELOGIO_HORA);
                snprintf(valor, sizeof(valor), "< %02d >", u->relogio.tm_hour);
                break;
            case 4:
                rotulo = T(MSG_RELOGIO_MINUTO);
                snprintf(valor, sizeof(valor), "< %02d >", u->relogio.tm_min);
                break;
            case 5:
                rotulo = T(MSG_RELOGIO_APLICAR);
                break;
            default:
                rotulo = T(MSG_SUBMENU_VOLTAR);
                break;
            }
            break;
        case TELA_TEMA:
            if (item < WM_TEMA_TOTAL) {
                rotulo = T(tema_msg_nome(item));
                if (item == tema_atual_id()) {
                    snprintf(valor, sizeof(valor), "%s", T(MSG_EM_USO));
                }
            } else {
                rotulo = T(MSG_SUBMENU_VOLTAR);
            }
            break;
        default:
            break;
        }
        linha_menu(u, r, rotulo, valor, sel, desativado);
        y += linha_h;
    }
    texto(c, u->f_pequeno, caixa.x + 16, caixa.y + caixa.h - 12, COR_MUTED,
          T(MSG_AJUDA_MENU));
}

static void desenhar_menu(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    Rect caixa;

    if (u->menu_tela == TELA_RAIZ) {
        int larg = l.compacto ? c->w - 40 : 420;
        int linha_h = l.compacto ? 26 : 32;
        int alt = 46 + MENU_ITENS * linha_h + 22;
        char valor[64];
        int y;

        if (alt > c->h - 20) {
            alt = c->h - 20;
        }
        caixa_modal(c, l, larg, alt, &caixa);
        texto(c, u->f_texto, caixa.x + 16, caixa.y + 26, COR_TEXTO, T(MSG_MENU));

        y = caixa.y + 40;
        for (int i = 0; i < MENU_ITENS; i++) {
            const char *rotulo;
            bool desativado;
            Rect r = { caixa.x + 10, y, caixa.w - 20, linha_h - 4 };

            rotulo_menu_raiz(u, i, &rotulo, valor, sizeof(valor), &desativado);
            linha_menu(u, r, rotulo, valor, u->menu_item == i, desativado);
            y += linha_h;
        }
        texto(c, u->f_pequeno, caixa.x + 16, caixa.y + caixa.h - 12, COR_MUTED,
              T(MSG_AJUDA_MENU));
        return;
    }

    {
        int larg = l.compacto ? c->w - 32 : 460;
        int alt = c->h - 2 * l.margem;
        const char *titulo = T(MSG_MENU);

        switch (u->menu_tela) {
        case TELA_WIFI:
            titulo = T(MSG_WIFI);
            break;
        case TELA_BT:
            titulo = T(MSG_BLUETOOTH);
            break;
        case TELA_SOM:
            titulo = T(MSG_SOM);
            break;
        case TELA_EQ:
            titulo = T(MSG_SOM_EQUALIZADOR);
            break;
        case TELA_IDIOMA:
            titulo = T(MSG_IDIOMA);
            break;
        case TELA_RELOGIO:
            titulo = T(MSG_RELOGIO);
            break;
        default:
            titulo = T(MSG_TEMA);
            break;
        }
        caixa_modal(c, l, larg, alt, &caixa);
        desenhar_submenu(u, l, caixa, titulo);
    }
}

static void desenhar_dialogo(Ui *u, Layout l)
{
    Canvas *c = u->canvas;
    int larg = l.compacto ? c->w - 80 : 380;
    int alt = l.compacto ? 150 : 170;
    Rect caixa;
    Rect botao;
    const char *opcoes[2];

    opcoes[0] = T(MSG_CANCELAR);
    opcoes[1] = u->dialogo_reiniciar ? T(MSG_REINICIAR) : T(MSG_DESLIGAR);

    caixa_modal(c, l, larg, alt, &caixa);
    texto_limitado(c, u->f_texto, caixa.x + 16, caixa.y + 30, COR_TEXTO,
                   u->dialogo_reiniciar ? T(MSG_CONFIRMA_REINICIAR)
                                        : T(MSG_CONFIRMA_DESLIGAR),
                   caixa.w - 32);
    texto(c, u->f_pequeno, caixa.x + 16, caixa.y + 52, COR_MUTED,
          u->dialogo_reiniciar ? T(MSG_REINICIANDO) : T(MSG_DESLIGANDO));

    for (int i = 0; i < 2; i++) {
        bool sel = (u->dialogo_item == i);
        int w = (caixa.w - 48) / 2;

        botao = (Rect) { caixa.x + 16 + i * (w + 16), caixa.y + alt - 56, w, 38 };
        gfx_fill_round(c, botao, 10, (i == 1) ? (sel ? COR_ACENTO_ESC : COR_ACENTO)
                                              : wm_rgba(255, 255, 255, sel ? 40 : 18));
        if (sel) {
            gfx_stroke_round(c, botao, 10, COR_TEXTO, 2);
        }
        {
            int tw = largura_texto(u->f_texto, opcoes[i]);
            texto(c, u->f_texto, botao.x + (botao.w - tw) / 2,
                  botao.y + botao.h / 2 + 6,
                  (i == 1) ? cor_contraste(COR_ACENTO) : COR_TEXTO, opcoes[i]);
        }
    }
}

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

    snprintf(buf, sizeof(buf), "%s %s", T(MSG_WIFI_DIGITE_SENHA), u->ssid_pendente);
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
          T(MSG_AJUDA_TECLADO));
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
        if (u->menu_aberto) {
            desenhar_menu(u, l);
        }
        if (u->dialogo_aberto) {
            desenhar_dialogo(u, l);
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
            snprintf(buf, sizeof(buf), "%s · %d %s%s", T(MSG_ABA_BIBLIOTECA), u->n_musicas, T(MSG_FAIXAS),
                     library_scanning(u->lib) ? " ..." : "");
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

                    if (!faixa_da_aba(u, posicao, &item)) {
                        break;
                    }
                    r = (Rect) { lista.x + 8, y, lista.w - 16, l.altura_linha - 6 };
                    sel = (u->foco == FOCO_LISTA && posicao == u->cursor_tecla);
                    if (sel) {
                        gfx_fill_round(c, r, 8, acento_alfa(45));
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
                          T(MSG_SEM_MUSICA));
                }
            }
            break;
        }
        case ABA_VIDEOS:
            desenhar_grade_videos(u, l, area);
            break;
        case ABA_BIBLIOTECA:
            desenhar_biblioteca(u, l);
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
    if (u->menu_aberto) {
        desenhar_menu(u, l);
    }
    if (u->dialogo_aberto) {
        desenhar_dialogo(u, l);
    }
}

/* --------------------------------------------------------------- ações --- */

/* Pede ao main para tocar o item sob o cursor. */
static void pedir_tocar(Ui *u, bool forcar)
{
    struct MediaItem item;

    if (u->foco != FOCO_LISTA && !forcar) {
        u->quer_toggle = true;
        return;
    }
    /* Biblioteca com o cursor na lista de ÁLBUNS: A abre o álbum (é assim que se
     * escolhe o álbum, como o usuário pediu); o play vem depois, nas faixas. */
    if (u->aba == ABA_BIBLIOTECA && !u->bib_aberto && !forcar) {
        if (u->bib_album >= 0 && u->bib_album < u->n_albuns) {
            u->bib_aberto = true;
            u->bib_faixa = 0;
            u->cursor_tecla = 0;
            u->bib_topo_faixas = 0;
        }
        return;
    }
    if (u->aba == ABA_BIBLIOTECA) {
        u->cursor_tecla = u->bib_faixa;
    }
    if (!faixa_da_aba(u, u->cursor_tecla, &item)) {
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

/* Ajusta o brilho respeitando o piso: abaixo de BRILHO_MIN o painel fica
 * ilegível (parece travado) e não economiza o que se espera — para isso existe
 * "Apagar a tela". Devolve o pedido para o main aplicar no backlight. */
static int ajustar_brilho(Ui *u, int delta)
{
    int atual = u->sys.brilho;

    if (atual < 0) {
        return -1;   /* sem backlight: nada a fazer */
    }
    atual += delta;
    if (atual < BRILHO_MIN) {
        atual = BRILHO_MIN;
    }
    if (atual > 100) {
        atual = 100;
    }
    u->sys.brilho = atual;      /* reflexo imediato, sem esperar o próximo poll */
    u->quer_brilho = atual;
    u->tem_quer_brilho = true;
    return atual;
}

static void recarregar_relogio(Ui *u)
{
    time_t agora = time(NULL);

    localtime_r(&agora, &u->relogio);
}

/* Grava o equalizador inteiro no arquivo de ajustes. É barato (poucas linhas) e
 * garante que o ajuste sobreviva ao botão de power, que desliga na hora. */
static void salvar_eq(void)
{
    char chave[16];

    for (int i = 0; i < WM_EQ_BANDAS; i++) {
        snprintf(chave, sizeof(chave), "eq%d", i);
        wm_settings_set_int(chave, (int) dsp_banda(i));
    }
    wm_settings_set_int("eq_preset", dsp_preset());
    wm_settings_set_int("preamp", (int) dsp_preamp());
    wm_settings_save();
}

/* Lê o equalizador do arquivo de ajustes (chamado no ui_create). */
static void carregar_eq(void)
{
    int preset = wm_settings_get_int("eq_preset", WM_PRESET_PLANO);
    char chave[16];

    if (preset == WM_PRESET_PERSONALIZADO) {
        /* O preset "personalizado" não tem tabela: os ganhos são os salvos. */
        dsp_aplicar_preset(WM_PRESET_PERSONALIZADO);
        for (int i = 0; i < WM_EQ_BANDAS; i++) {
            snprintf(chave, sizeof(chave), "eq%d", i);
            dsp_set_banda(i, (float) wm_settings_get_int(chave, 0));
        }
    } else {
        dsp_aplicar_preset(preset);
    }
    dsp_set_preamp((float) wm_settings_get_int("preamp", 0));
}

static void abrir_menu(Ui *u)
{
    u->menu_aberto = true;
    u->dialogo_aberto = false;
    u->menu_tela = TELA_RAIZ;
    u->menu_item = MENU_BRILHO;
    u->menu_curso = 0;
    recarregar_relogio(u);
}

static void fechar_menu(Ui *u)
{
    u->menu_aberto = false;
    u->dialogo_aberto = false;
    u->menu_tela = TELA_RAIZ;
}

static void abrir_dialogo(Ui *u, bool reiniciar)
{
    u->dialogo_aberto = true;
    u->dialogo_reiniciar = reiniciar;
    /* O foco começa em "cancelar": um toque errado não desliga o aparelho. */
    u->dialogo_item = 0;
}

static void entrar_submenu(Ui *u, int tela)
{
    u->menu_tela = tela;
    u->menu_curso = 0;
    if (tela == TELA_WIFI) {
        radio_scan_wifi(u->radio);
    } else if (tela == TELA_BT) {
        u->n_bt_devs = radio_bt_dispositivos(u->radio, u->bt_devs, BT_DEV_MAX);
    } else if (tela == TELA_RELOGIO) {
        recarregar_relogio(u);
    }
}

/* Volta um nível: do equalizador para o som, dos submenus para a raiz. */
static void voltar_submenu(Ui *u)
{
    if (u->menu_tela == TELA_EQ) {
        u->menu_tela = TELA_SOM;
        u->menu_curso = 2;
        return;
    }
    if (u->menu_tela == TELA_RAIZ) {
        fechar_menu(u);
        return;
    }
    u->menu_tela = TELA_RAIZ;
    u->menu_curso = 0;
}

/* Executa o item selecionado da raiz. Devolve a ação para o main. */
static int ativar_item_menu(Ui *u)
{
    switch (u->menu_item) {
    case MENU_BRILHO:
        /* Enter no brilho sobe 5% (◀▶ ajusta nos dois sentidos). */
        ajustar_brilho(u, 5);
        return WM_ACAO_BRILHO;
    case MENU_WIFI:
        entrar_submenu(u, TELA_WIFI);
        return WM_ACAO_NENHUMA;
    case MENU_BT:
        entrar_submenu(u, TELA_BT);
        return WM_ACAO_NENHUMA;
    case MENU_SOM:
        entrar_submenu(u, TELA_SOM);
        return WM_ACAO_NENHUMA;
    case MENU_IDIOMA:
        entrar_submenu(u, TELA_IDIOMA);
        return WM_ACAO_NENHUMA;
    case MENU_RELOGIO:
        entrar_submenu(u, TELA_RELOGIO);
        return WM_ACAO_NENHUMA;
    case MENU_TEMA:
        entrar_submenu(u, TELA_TEMA);
        return WM_ACAO_NENHUMA;
    case MENU_APAGAR:
        u->menu_aberto = false;
        u->tela_apagada = true;
        return WM_ACAO_TELA_APAGAR;
    case MENU_DESLIGAR:
        abrir_dialogo(u, false);
        return WM_ACAO_NENHUMA;
    case MENU_REINICIAR:
        abrir_dialogo(u, true);
        return WM_ACAO_NENHUMA;
    default:
        fechar_menu(u);
        return WM_ACAO_NENHUMA;
    }
}

/* Quantos dias tem o mês de `mes` (0..11) no ano `ano`. */
static int dias_no_mes(int mes, int ano)
{
    static const int DIAS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (mes < 0 || mes > 11) {
        return 31;
    }
    if (mes == 1 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) {
        return 29;
    }
    return DIAS[mes];
}

/* Ajusta um campo do relógio em `delta`. O dia é validado contra o mês para não
 * gravar 31 de fevereiro (mktime normalizaria e o usuário não entenderia). */
static void ajustar_relogio(Ui *u, int delta)
{
    switch (u->menu_curso) {
    case 0:
        u->relogio.tm_year += delta;
        if (u->relogio.tm_year < 100) {
            u->relogio.tm_year = 100;      /* 2000 */
        }
        if (u->relogio.tm_year > 199) {
            u->relogio.tm_year = 199;      /* 2099 */
        }
        break;
    case 1:
        u->relogio.tm_mon += delta;
        if (u->relogio.tm_mon < 0) {
            u->relogio.tm_mon = 11;
        }
        if (u->relogio.tm_mon > 11) {
            u->relogio.tm_mon = 0;
        }
        break;
    case 2: {
        int meses = dias_no_mes(u->relogio.tm_mon, u->relogio.tm_year + 1900);

        u->relogio.tm_mday += delta;
        if (u->relogio.tm_mday < 1) {
            u->relogio.tm_mday = meses;
        }
        if (u->relogio.tm_mday > meses) {
            u->relogio.tm_mday = 1;
        }
        break;
    }
    case 3:
        u->relogio.tm_hour = (u->relogio.tm_hour + delta + 24) % 24;
        break;
    default:
        u->relogio.tm_min = (u->relogio.tm_min + delta + 60) % 60;
        break;
    }
    {
        int meses = dias_no_mes(u->relogio.tm_mon, u->relogio.tm_year + 1900);

        if (u->relogio.tm_mday > meses) {
            u->relogio.tm_mday = meses;
        }
    }
}

/* Executa o item do submenu aberto. */
static int ativar_submenu(Ui *u)
{
    int n = submenu_linhas(u, u->menu_tela);

    if (u->menu_curso < 0 || u->menu_curso >= n) {
        return WM_ACAO_NENHUMA;
    }
    switch (u->menu_tela) {
    case TELA_WIFI:
        if (u->menu_curso == 0) {
            radio_scan_wifi(u->radio);
        } else if (u->menu_curso <= u->n_redes) {
            struct Rede *rede = &u->redes[u->menu_curso - 1];

            if (rede->seguranca[0] && wm_casecmp(rede->seguranca, "aberta") != 0
                && wm_casecmp(rede->seguranca, "--") != 0) {
                abrir_teclado(u, rede->ssid);
            } else {
                radio_conectar_wifi(u->radio, rede->ssid, "");
            }
        } else {
            voltar_submenu(u);
        }
        break;
    case TELA_BT:
        if (u->menu_curso == 0) {
            radio_bt_power(u->radio, !u->sys.bt_ligado);
        } else if (u->menu_curso == 1) {
            if (u->sys.bt_ligado) {
                radio_bt_scan(u->radio);
            }
        } else if (u->menu_curso <= u->n_bt_devs) {
            radio_bt_connect(u->radio, u->bt_devs[u->menu_curso - 2].mac);
        } else {
            voltar_submenu(u);
        }
        break;
    case TELA_SOM:
        switch (u->menu_curso) {
        case 0:
            dsp_aplicar_preset((dsp_preset() + 1) % WM_PRESET_TOTAL);
            salvar_eq();
            break;
        case 1:
            dsp_set_preamp(dsp_preamp() + 1.0f);
            wm_settings_set_int("preamp", (int) dsp_preamp());
            wm_settings_save();
            break;
        case 2:
            entrar_submenu(u, TELA_EQ);
            break;
        case 3:
            if (dsp_neural_disponivel()) {
                dsp_set_neural(!dsp_neural());
                wm_settings_set_int("neural", dsp_neural() ? 1 : 0);
                wm_settings_save();
            }
            break;
        case 4:
            dsp_reset();
            salvar_eq();
            break;
        default:
            voltar_submenu(u);
            break;
        }
        break;
    case TELA_EQ:
        if (u->menu_curso < WM_EQ_BANDAS) {
            dsp_set_banda(u->menu_curso, 0.0f);   /* A zera a banda */
            salvar_eq();
        } else {
            voltar_submenu(u);
        }
        break;
    case TELA_IDIOMA:
        if (u->menu_curso < WM_LANG_TOTAL) {
            wm_lang_set(u->menu_curso);
            wm_settings_set("idioma", wm_lang_code(u->menu_curso));
            wm_settings_save();
        } else {
            voltar_submenu(u);
        }
        break;
    case TELA_RELOGIO:
        if (u->menu_curso == 5) {
            wm_clock_set(&u->relogio);
        } else if (u->menu_curso == 6) {
            voltar_submenu(u);
        }
        break;
    case TELA_TEMA:
        if (u->menu_curso < WM_TEMA_TOTAL) {
            tema_set(u->menu_curso);
            wm_settings_set("tema", tema_codigo(u->menu_curso));
            wm_settings_save();
        } else {
            voltar_submenu(u);
        }
        break;
    default:
        voltar_submenu(u);
        break;
    }
    return WM_ACAO_NENHUMA;
}

/* ◀▶ dentro do submenu. */
static void ajustar_submenu(Ui *u, int delta)
{
    switch (u->menu_tela) {
    case TELA_SOM:
        if (u->menu_curso == 0) {
            int p = dsp_preset() + delta;

            if (p < 0) {
                p = WM_PRESET_TOTAL - 1;
            }
            if (p >= WM_PRESET_TOTAL) {
                p = 0;
            }
            dsp_aplicar_preset(p);
            salvar_eq();
        } else if (u->menu_curso == 1) {
            dsp_set_preamp(dsp_preamp() + (float) delta);
            wm_settings_set_int("preamp", (int) dsp_preamp());
            wm_settings_save();
        }
        break;
    case TELA_EQ:
        if (u->menu_curso < WM_EQ_BANDAS) {
            dsp_set_banda(u->menu_curso, dsp_banda(u->menu_curso) + (float) delta);
            salvar_eq();
        }
        break;
    case TELA_RELOGIO:
        ajustar_relogio(u, delta);
        break;
    case TELA_TEMA:
        {
            int t = tema_atual_id() + delta;

            if (t < 0) {
                t = WM_TEMA_TOTAL - 1;
            }
            if (t >= WM_TEMA_TOTAL) {
                t = 0;
            }
            tema_set(t);
        }
        break;
    case TELA_IDIOMA:
        {
            int l = wm_lang() + delta;

            if (l < 0) {
                l = WM_LANG_TOTAL - 1;
            }
            if (l >= WM_LANG_TOTAL) {
                l = 0;
            }
            wm_lang_set(l);
            u->menu_curso = l;
        }
        break;
    default:
        break;
    }
}

/* Diálogo de confirmação: 0 = cancelar, 1 = desligar/reiniciar. */
static int ativar_dialogo(Ui *u)
{
    int escolha = u->dialogo_item;
    bool reiniciar = u->dialogo_reiniciar;

    u->dialogo_aberto = false;
    u->menu_aberto = false;
    if (escolha != 1) {
        return WM_ACAO_NENHUMA;
    }
    return reiniciar ? WM_ACAO_REINICIAR : WM_ACAO_DESLIGAR;
}

/* --------------------------------------------------------------- teclas --- */

int ui_handle_key(Ui *u, const struct KeyEvent *ev)
{
    int acao = WM_ACAO_NENHUMA;
    int n;

    if (ev->soltou) {
        return WM_ACAO_NENHUMA;
    }

    /* Power: toque curto apaga/acende a tela; segurar 3 s abre o diálogo de
     * desligar (o mesmo que o item do menu). Fica antes de tudo para funcionar
     * em qualquer tela, inclusive no vídeo em tela cheia. */
    if (ev->key == WM_KEY_POWER_CURTO) {
        if (u->tela_apagada) {
            u->tela_apagada = false;
            return WM_ACAO_TELA_ACENDER;
        }
        /* Com menu/diálogo aberto, apagar a tela esconderia o diálogo: fecha. */
        if (u->dialogo_aberto || u->menu_aberto) {
            fechar_menu(u);
            return WM_ACAO_TELA_ACENDER;
        }
        u->tela_apagada = true;
        return WM_ACAO_TELA_APAGAR;
    }
    if (ev->key == WM_KEY_POWER_LONGO) {
        u->tela_apagada = false;
        u->menu_aberto = false;
        abrir_dialogo(u, false);
        return WM_ACAO_TELA_ACENDER;
    }

    /* Tela apagada: qualquer outra tecla acende (é o que o usuário espera). */
    if (u->tela_apagada) {
        u->tela_apagada = false;
        return WM_ACAO_TELA_ACENDER;
    }

    /* Diálogo de confirmação: tem o foco. */
    if (u->dialogo_aberto) {
        switch (ev->key) {
        case WM_KEY_ESQ:
        case WM_KEY_DIR:
            u->dialogo_item = u->dialogo_item ? 0 : 1;
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            return ativar_dialogo(u);
        case WM_KEY_VOLTAR:
            u->dialogo_aberto = false;
            break;
        default:
            /* Volume continua funcionando com o diálogo aberto. */
            if (ev->key == WM_KEY_VOL_MAIS) {
                u->quer_volume = audio_volume(u->audio) + 5;
                u->tem_quer_volume = true;
            } else if (ev->key == WM_KEY_VOL_MENOS) {
                u->quer_volume = audio_volume(u->audio) - 5;
                u->tem_quer_volume = true;
            }
            break;
        }
        return WM_ACAO_NENHUMA;
    }

    /* Teclado na tela antes do menu: ele é aberto DE DENTRO do submenu de Wi-Fi,
     * e com o menu na frente as teclas nunca chegariam ao teclado. */
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

    /* Menu de configurações: tem o foco. A raiz navega em vertical; os
     * submenus usam ◀▶ para mudar valor e A para confirmar. */
    if (u->menu_aberto) {
        int linhas = submenu_linhas(u, u->menu_tela);

        if (ev->key == WM_KEY_VOL_MAIS) {
            u->quer_volume = audio_volume(u->audio) + 5;
            u->tem_quer_volume = true;
            return WM_ACAO_NENHUMA;
        }
        if (ev->key == WM_KEY_VOL_MENOS) {
            u->quer_volume = audio_volume(u->audio) - 5;
            u->tem_quer_volume = true;
            return WM_ACAO_NENHUMA;
        }

        if (u->menu_tela == TELA_RAIZ) {
            switch (ev->key) {
            case WM_KEY_CIMA:
                u->menu_item = (u->menu_item + MENU_ITENS - 1) % MENU_ITENS;
                break;
            case WM_KEY_BAIXO:
                u->menu_item = (u->menu_item + 1) % MENU_ITENS;
                break;
            case WM_KEY_ESQ:
                if (u->menu_item == MENU_BRILHO) {
                    ajustar_brilho(u, -5);
                    return WM_ACAO_BRILHO;
                }
                break;
            case WM_KEY_DIR:
                if (u->menu_item == MENU_BRILHO) {
                    ajustar_brilho(u, 5);
                    return WM_ACAO_BRILHO;
                }
                break;
            case WM_KEY_OK:
            case WM_KEY_ENTER:
                return ativar_item_menu(u);
            case WM_KEY_VOLTAR:
            case WM_KEY_MENU:
                fechar_menu(u);
                break;
            default:
                break;
            }
            return WM_ACAO_NENHUMA;
        }

        switch (ev->key) {
        case WM_KEY_CIMA:
            if (linhas > 0) {
                u->menu_curso = (u->menu_curso + linhas - 1) % linhas;
            }
            break;
        case WM_KEY_BAIXO:
            if (linhas > 0) {
                u->menu_curso = (u->menu_curso + 1) % linhas;
            }
            break;
        case WM_KEY_ESQ:
            ajustar_submenu(u, -1);
            break;
        case WM_KEY_DIR:
            ajustar_submenu(u, 1);
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            return ativar_submenu(u);
        case WM_KEY_VOLTAR:
            voltar_submenu(u);
            break;
        case WM_KEY_MENU:
            fechar_menu(u);
            break;
        default:
            break;
        }
        return WM_ACAO_NENHUMA;
    }

    /* SELECT abre as configurações em qualquer aba. */
    if (ev->key == WM_KEY_MENU) {
        abrir_menu(u);
        return WM_ACAO_NENHUMA;
    }

    /* Tela cheia só vale na aba de vídeos: trocar de aba (L/R) sai dela. Sem
     * esta checagem a UI ficava "presa" em tela cheia e nenhuma tecla da nova
     * aba funcionava (o estado continuava ligado). */
    if (u->video_tela_cheia && u->aba == ABA_VIDEOS) {
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
        /* L1/R1 no vídeo: avançar/voltar rápido (30 s). */
        case WM_KEY_PULO_PROX:
            u->quer_seek = 30;
            u->tem_quer_seek = true;
            u->osd_ate = wm_now() + 3.0;
            break;
        case WM_KEY_PULO_ANT:
            u->quer_seek = -30;
            u->tem_quer_seek = true;
            u->osd_ate = wm_now() + 3.0;
            break;
        default:
            break;
        }
        return WM_ACAO_NENHUMA;
    }

    n = itens_da_aba(u);

    switch (ev->key) {
    case WM_KEY_ABA_PROX:
        u->aba = (u->aba + 1) % TELAS;
        u->foco = FOCO_LISTA;
        u->cursor_tecla = 0;
        u->topo_lista = 0;
        return WM_ACAO_NENHUMA;
    case WM_KEY_ABA_ANT:
        u->aba = (u->aba - 1 + TELAS) % TELAS;
        u->foco = FOCO_LISTA;
        u->cursor_tecla = 0;
        u->topo_lista = 0;
        return WM_ACAO_NENHUMA;
    case WM_KEY_VOLTAR:
        /* B na Biblioteca com o álbum aberto volta para a lista de álbuns; nas
         * outras telas, sai do app. */
        if (u->aba == ABA_BIBLIOTECA && u->bib_aberto) {
            u->bib_aberto = false;
            u->cursor_tecla = 0;
            return WM_ACAO_NENHUMA;
        }
        return WM_ACAO_SAIR;
    case WM_KEY_TOCAR_PAUSAR:
        u->quer_toggle = true;
        return WM_ACAO_NENHUMA;
    case WM_KEY_VOL_MAIS:
        u->quer_volume = audio_volume(u->audio) + 5;
        u->tem_quer_volume = true;
        return WM_ACAO_NENHUMA;
    case WM_KEY_VOL_MENOS:
        u->quer_volume = audio_volume(u->audio) - 5;
        u->tem_quer_volume = true;
        return WM_ACAO_NENHUMA;
    default:
        break;
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
                if (u->foco_botao < 4) {
                    u->foco_botao++;
                }
            } else if (colunas > 1 && u->cursor_tecla + 1 < n) {
                u->cursor_tecla++;
            }
            break;
        case WM_KEY_OK:
        case WM_KEY_ENTER:
            if (u->foco == FOCO_CONTROLES) {
                /* 0 = anterior, 1 = play/pause, 2 = próximo, 3 = parar,
                 * 4 = modo de reprodução (em ordem / repetir álbum / repetir
                 * faixa / aleatório). */
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
                case 3:
                    u->quer_parar = true;
                    break;
                default:
                    ui_set_modo(u, (u->modo + 1) % WM_MODO_TOTAL);
                    break;
                }
            } else {
                pedir_tocar(u, false);
            }
            break;
        /* L1/R1: na música salta de faixa; na grade, pula uma página. */
        case WM_KEY_PULO_PROX:
            if (u->aba == ABA_MUSICA) {
                u->quer_proximo = true;
            } else {
                u->cursor_tecla += colunas * 4;
                if (u->cursor_tecla >= n) {
                    u->cursor_tecla = n > 0 ? n - 1 : 0;
                }
            }
            break;
        case WM_KEY_PULO_ANT:
            if (u->aba == ABA_MUSICA) {
                u->quer_anterior = true;
            } else {
                u->cursor_tecla -= colunas * 4;
                if (u->cursor_tecla < 0) {
                    u->cursor_tecla = 0;
                }
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
    case ABA_BIBLIOTECA:
        /* Fechada: o cursor anda pelos ÁLBUNS. Aberta: pelas faixas. */
        if (!u->bib_aberto) {
            switch (ev->key) {
            case WM_KEY_CIMA:
                if (u->bib_album > 0) {
                    u->bib_album--;
                }
                break;
            case WM_KEY_BAIXO:
                if (u->bib_album + 1 < u->n_albuns) {
                    u->bib_album++;
                }
                break;
            case WM_KEY_OK:
            case WM_KEY_ENTER:
                pedir_tocar(u, false);   /* abre o álbum */
                break;
            case WM_KEY_PULO_PROX:
            case WM_KEY_PG_BAIXO:
                u->bib_album += 5;
                if (u->bib_album >= u->n_albuns) {
                    u->bib_album = u->n_albuns > 0 ? u->n_albuns - 1 : 0;
                }
                break;
            case WM_KEY_PULO_ANT:
            case WM_KEY_PG_CIMA:
                u->bib_album -= 5;
                if (u->bib_album < 0) {
                    u->bib_album = 0;
                }
                break;
            default:
                break;
            }
        } else {
            int faixas = u->alb_n[u->bib_album];

            switch (ev->key) {
            case WM_KEY_CIMA:
                if (u->bib_faixa > 0) {
                    u->bib_faixa--;
                }
                break;
            case WM_KEY_BAIXO:
                if (u->bib_faixa + 1 < faixas) {
                    u->bib_faixa++;
                }
                break;
            case WM_KEY_OK:
            case WM_KEY_ENTER:
                u->cursor_tecla = u->bib_faixa;
                pedir_tocar(u, false);
                break;
            case WM_KEY_ESQ:
                /* ◀ volta para a lista de álbuns (o mesmo que B). */
                u->bib_aberto = false;
                u->cursor_tecla = 0;
                break;
            case WM_KEY_PULO_PROX:
                u->quer_proximo = true;
                break;
            case WM_KEY_PULO_ANT:
                u->quer_anterior = true;
                break;
            case WM_KEY_PG_BAIXO:
                u->bib_faixa += 5;
                if (u->bib_faixa >= faixas) {
                    u->bib_faixa = faixas > 0 ? faixas - 1 : 0;
                }
                break;
            case WM_KEY_PG_CIMA:
                u->bib_faixa -= 5;
                if (u->bib_faixa < 0) {
                    u->bib_faixa = 0;
                }
                break;
            default:
                break;
            }
            u->cursor_tecla = u->bib_faixa;
        }
        break;
    case ABA_SISTEMA:
        /* Nada de ajuste aqui: o botão de volume era confundido com brilho
         * porque ambos estavam mapeados nesta aba. Ajustes ficam no SELECT. */
        switch (ev->key) {
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

int ui_modo(const Ui *u)
{
    return u ? u->modo : WM_MODO_SEQUENCIAL;
}

void ui_set_modo(Ui *u, int modo)
{
    if (!u) {
        return;
    }
    if (modo < 0 || modo >= WM_MODO_TOTAL) {
        modo = WM_MODO_SEQUENCIAL;
    }
    u->modo = modo;
    wm_settings_set_int("modo", modo);
    wm_settings_save();
}

/* ------------------------------------------------------------- ligação --- */

/* Fontes de reserva por alfabeto. DejaVu é a primária; estas cobrem o que ela
 * não tem. O pacote é fonts-noto-core (árabe, devanágari, bengali) mais
 * fonts-wqy-zenhei para o chinês — ~58 MB, contra ~100 MB do fonts-noto-cjk. */
static const char *const FONTES_RESERVA[] = {
    "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansBengali-Regular.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    NULL
};

Ui *ui_create(Canvas *canvas, Library *lib, AudioOut *audio)
{
    Ui *u = calloc(1, sizeof(*u));
    static const char *caminhos_fonte[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL
    };
    Font **fontes[5];
    const char *fonte = NULL;
    bool compacto;

    if (!u) {
        return NULL;
    }
    u->canvas = canvas;
    u->lib = lib;
    u->audio = audio;
    compacto = canvas->w < 900;
    u->modo = WM_MODO_SEQUENCIAL;

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

    fontes[0] = &u->f_titulo;
    fontes[1] = &u->f_grande;
    fontes[2] = &u->f_texto;
    fontes[3] = &u->f_pequeno;
    fontes[4] = &u->f_numero;
    for (int i = 0; i < 5; i++) {
        if (!*fontes[i]) {
            continue;
        }
        for (int r = 0; FONTES_RESERVA[r]; r++) {
            if (access(FONTES_RESERVA[r], R_OK) == 0) {
                font_add_fallback(*fontes[i], FONTES_RESERVA[r]);
            }
        }
    }

    /* Ajustes que o app grava em /boot/walkmam-media.conf. O idioma e o tema
     * precisam valer ANTES do primeiro desenho; o EQ e o modo podem vir depois,
     * mas são baratos e ficam aqui. */
    wm_settings_load();
    wm_lang_set(wm_lang_from_code(wm_settings_get("idioma", "pt")));
    tema_set(tema_por_codigo(wm_settings_get("tema", "neon")));
    u->modo = wm_settings_get_int("modo", WM_MODO_SEQUENCIAL);
    if (u->modo < 0 || u->modo >= WM_MODO_TOTAL) {
        u->modo = WM_MODO_SEQUENCIAL;
    }
    carregar_eq();
    if (wm_settings_get_int("neural", 0) && dsp_neural_disponivel()) {
        dsp_set_neural(true);
    }

    u->n_redes = 0;
    u->bib_album = 0;
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
    u->n_bt_devs = sys->bt_presente
                       ? radio_bt_dispositivos(u->radio, u->bt_devs, BT_DEV_MAX)
                       : 0;
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

bool ui_tela_apagada(Ui *u)
{
    return u && u->tela_apagada;
}

int ui_brilho(Ui *u)
{
    if (!u) {
        return -1;
    }
    return u->sys.brilho;
}

int ui_want_brightness(Ui *u)
{
    int v;

    if (!u || !u->tem_quer_brilho) {
        return -1;
    }
    v = u->quer_brilho;
    u->tem_quer_brilho = false;
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
    int n = faixas_da_aba(u);

    if (!u || n <= 0) {
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        if (faixa_da_aba(u, i, &item) && u->tocando_path[0]
            && strcmp(item.caminho, u->tocando_path) == 0) {
            atual = i;
            break;
        }
    }
    if (atual < 0) {
        atual = u->cursor_tecla;
    }
    atual = (atual + delta % n + n) % n;
    if (!faixa_da_aba(u, atual, &item)) {
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
