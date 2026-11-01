/* font.c — texto na CPU via FreeType + HarfBuzz.
 *
 * Cada glifo é rasterizado uma vez para um bitmap de cobertura 8 bits e fica no
 * cache; desenhar texto é, então, só blending de máscaras. Isso importa porque a
 * UI inteira (listas, OSD, relógio) é redesenhada a cada quadro e não existe GPU
 * em uso.
 *
 * Por que HarfBuzz: a UI fala dez idiomas, e três deles não funcionam com
 * "um glifo por codepoint":
 *   - árabe e urdu: as letras se JUNTAM (formas inicial/medial/final/isolada);
 *   - hindi e bengali: as vogais são REORDENADAS em volta da consoante e há
 *     ligaduras obrigatórias.
 * O FreeType só rasteriza um índice de glifo; quem escolhe *qual* índice usar é o
 * HarfBuzz. Sem ele, "مرحبا" sai como letras soltas e "हिन्दी" sai com as
 * vogais no lugar errado.
 *
 * Por que as fontes de reserva: DejaVu não tem árabe, devanágari, bengali nem
 * chinês. Cada Font carrega até cinco faces (a primária + reservas) e o texto é
 * fatiado em trechos de mesmo alfabeto; cada trecho é desenhado com a primeira
 * face que cobre aquele alfabeto. Assim o mesmo rótulo pode misturar "Wi-Fi "
 * (DejaVu) com "شبكة" (Noto Arabic) sem trocar de fonte no código da UI.
 */

#include "wm.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#define GLIFOS_MAX 768
#define FACES_MAX 6

typedef struct {
    int face;        /* índice em Font::faces */
    uint32_t indice; /* índice do glifo NAQUELA face */
    int w, h;
    int bearing_x, bearing_y;
    int avanco;
    uint8_t *bitmap;
} Glifo;

struct Font {
    FT_Face faces[FACES_MAX];
    hb_font_t *hb[FACES_MAX];
    int n_faces;
    int tamanho;
    int altura_linha;
    int ascent;
    Glifo cache[GLIFOS_MAX];
    int n_cache;
    int proximo;   /* rodízio do cache quando enche */
};

static FT_Library g_ft;
static int g_ft_refs;

/* Grupos de alfabeto: só o suficiente para escolher a face. */
enum {
    GRUPO_COMUM = 0,
    GRUPO_ARABE,
    GRUPO_DEVANAGARI,
    GRUPO_BENGALI,
    GRUPO_CJK
};

static bool ft_garantir(void)
{
    if (g_ft) {
        g_ft_refs++;
        return true;
    }
    if (FT_Init_FreeType(&g_ft) != 0) {
        wm_log("!!! FreeType nao inicializou: sem texto na UI");
        g_ft = NULL;
        return false;
    }
    g_ft_refs = 1;
    return true;
}

static int grupo_de(uint32_t cp)
{
    if (cp >= 0x0600 && cp <= 0x06FF) {
        return GRUPO_ARABE;
    }
    if (cp >= 0x0750 && cp <= 0x077F) {
        return GRUPO_ARABE;
    }
    if (cp >= 0x08A0 && cp <= 0x08FF) {
        return GRUPO_ARABE;
    }
    if (cp >= 0xFB50 && cp <= 0xFDFF) {
        return GRUPO_ARABE;
    }
    if (cp >= 0xFE70 && cp <= 0xFEFF) {
        return GRUPO_ARABE;
    }
    if (cp >= 0x0900 && cp <= 0x097F) {
        return GRUPO_DEVANAGARI;
    }
    if (cp >= 0x0980 && cp <= 0x09FF) {
        return GRUPO_BENGALI;
    }
    if (cp >= 0x2E80 && cp <= 0x9FFF) {
        return GRUPO_CJK;
    }
    if (cp >= 0xF900 && cp <= 0xFAFF) {
        return GRUPO_CJK;
    }
    if (cp >= 0xFF00 && cp <= 0xFFEF) {
        return GRUPO_CJK;
    }
    if (cp >= 0x20000 && cp <= 0x3FFFF) {
        return GRUPO_CJK;
    }
    return GRUPO_COMUM;
}

static bool face_cobre(FT_Face face, uint32_t cp)
{
    return face && FT_Get_Char_Index(face, cp) != 0;
}

static int face_para_grupo(Font *f, int grupo, uint32_t amostra)
{
    if (grupo == GRUPO_COMUM) {
        return 0;
    }
    for (int i = 0; i < f->n_faces; i++) {
        if (face_cobre(f->faces[i], amostra)) {
            return i;
        }
    }
    /* Nenhuma face cobre: desenha com a primária (sai o "?" da própria fonte,
     * que é melhor do que nada e não trava a UI). */
    return 0;
}

static bool font_adicionar_face(Font *f, const char *path)
{
    FT_Face face;
    hb_font_t *hb;

    if (!f || !path || f->n_faces >= FACES_MAX) {
        return false;
    }
    if (FT_New_Face(g_ft, path, 0, &face) != 0) {
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt) f->tamanho);
    hb = hb_ft_font_create_referenced(face);
    if (!hb) {
        FT_Done_Face(face);
        return false;
    }
    /* O rasterizado com hint vem do FreeType em FT_Load_Glyph; o HarfBuzz só
     * posiciona, então ele não deve aplicar hint próprio. */
    hb_ft_font_set_load_flags(hb, FT_LOAD_DEFAULT);
    f->faces[f->n_faces] = face;
    f->hb[f->n_faces] = hb;
    f->n_faces++;
    return true;
}

Font *font_open(const char *path, int tamanho_px)
{
    Font *f;

    if (!path || tamanho_px <= 0) {
        return NULL;
    }
    if (!ft_garantir()) {
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    if (!f) {
        g_ft_refs--;
        return NULL;
    }
    f->tamanho = tamanho_px;
    if (!font_adicionar_face(f, path)) {
        wm_log("fonte nao encontrada: %s", path);
        free(f);
        g_ft_refs--;
        return NULL;
    }
    /* Altura de linha = altura da fonte + folga; ascent vem em 26.6. */
    f->altura_linha = (int) ((f->faces[0]->size->metrics.height + 63) >> 6);
    f->ascent = (int) ((f->faces[0]->size->metrics.ascender + 63) >> 6);
    if (f->altura_linha <= 0) {
        f->altura_linha = tamanho_px + 2;
    }
    return f;
}

void font_add_fallback(Font *f, const char *path)
{
    if (!f || !path) {
        return;
    }
    if (!font_adicionar_face(f, path)) {
        /* Silencioso de propósito: fonte de reserva ausente só significa que
         * aquele alfabeto sai como "?". Fica no log para o diagnóstico. */
        wm_log("fonte de reserva indisponivel: %s", path);
    }
}

void font_close(Font *f)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < f->n_cache; i++) {
        free(f->cache[i].bitmap);
    }
    for (int i = 0; i < f->n_faces; i++) {
        if (f->hb[i]) {
            hb_font_destroy(f->hb[i]);
        }
        if (f->faces[i]) {
            FT_Done_Face(f->faces[i]);
        }
    }
    free(f);
    g_ft_refs--;
    if (g_ft_refs <= 0 && g_ft) {
        FT_Done_FreeType(g_ft);
        g_ft = NULL;
        g_ft_refs = 0;
    }
}

int font_line_height(const Font *f)
{
    return f ? f->altura_linha : 0;
}

int font_ascent(const Font *f)
{
    return f ? f->ascent : 0;
}

/* --------------------------------------------------------------- glifos --- */

static Glifo *glifo_obter(Font *f, int face, uint32_t indice)
{
    Glifo *g;

    for (int i = 0; i < f->n_cache; i++) {
        if (f->cache[i].face == face && f->cache[i].indice == indice) {
            return &f->cache[i];
        }
    }
    if (f->n_cache < GLIFOS_MAX) {
        g = &f->cache[f->n_cache++];
    } else {
        /* Cache cheio: reaproveita em rodízio (as listas longas trocam de
         * texto a cada tela; expulsar sempre o primeiro daria mais recarga). */
        g = &f->cache[f->proximo];
        f->proximo = (f->proximo + 1) % GLIFOS_MAX;
        free(g->bitmap);
    }
    memset(g, 0, sizeof(*g));
    g->face = face;
    g->indice = indice;

    if (indice == 0 || FT_Load_Glyph(f->faces[face], (FT_UInt) indice, FT_LOAD_RENDER) != 0) {
        g->indice = 0;
        return NULL;
    }
    {
        FT_GlyphSlot slot = f->faces[face]->glyph;

        g->w = (int) slot->bitmap.width;
        g->h = (int) slot->bitmap.rows;
        g->bearing_x = slot->bitmap_left;
        g->bearing_y = slot->bitmap_top;
        g->avanco = (int) (slot->advance.x >> 6);
        if (g->w > 0 && g->h > 0) {
            size_t tam = (size_t) g->w * (size_t) g->h;
            g->bitmap = malloc(tam);
            if (g->bitmap) {
                /* A máscara do FreeType pode ter pitch > width. */
                for (int y = 0; y < g->h; y++) {
                    memcpy(g->bitmap + (size_t) y * g->w,
                           slot->bitmap.buffer + (size_t) y * slot->bitmap.pitch,
                           (size_t) g->w);
                }
            }
        }
    }
    return g;
}

/* --------------------------------------------------------------- shaping --- */

/* Um trecho é uma fatia do texto com um único alfabeto (e, portanto, uma única
 * face). `bytes` conta bytes, não caracteres. */
typedef struct {
    const char *ini;
    size_t bytes;
    int grupo;
    uint32_t amostra;   /* primeiro codepoint "forte" do trecho */
} Trecho;

#define TRECHOS_MAX 64

/* Preenche até `max` trechos. Devolve quantos. */
static int fatiar(const char *texto, Trecho *out, int max)
{
    int n = 0;
    const char *p = texto;

    if (!texto) {
        return 0;
    }
    while (*p && n < max) {
        uint32_t cp;
        int bytes = wm_utf8_decode(p, &cp);
        int grupo;
        const char *ini;
        uint32_t amostra;

        if (bytes <= 0) {
            break;
        }
        grupo = grupo_de(cp);
        ini = p;
        amostra = (grupo == GRUPO_COMUM) ? 0 : cp;
        p += bytes;
        /* Espaço/pontuação no meio de um trecho estrangeiro não deve quebrá-lo
         * ("Rede 1 شبكة" continua um trecho árabe a partir de شبكة). */
        while (*p) {
            uint32_t cp2;
            int b2 = wm_utf8_decode(p, &cp2);
            int g2;

            if (b2 <= 0) {
                break;
            }
            g2 = grupo_de(cp2);
            if (g2 != grupo) {
                if (g2 == GRUPO_COMUM) {
                    /* Comum cola nos dois lados. */
                    p += b2;
                    continue;
                }
                break;
            }
            if (amostra == 0) {
                amostra = cp2;
            }
            p += b2;
        }
        out[n].ini = ini;
        out[n].bytes = (size_t) (p - ini);
        out[n].grupo = grupo;
        out[n].amostra = amostra;
        n++;
    }
    return n;
}

static hb_buffer_t *shaping(Font *f, int face, const char *ini, size_t bytes)
{
    hb_buffer_t *buf = hb_buffer_create();

    if (!buf) {
        return NULL;
    }
    hb_buffer_add_utf8(buf, ini, (int) bytes, 0, (int) bytes);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(f->hb[face], buf, NULL, 0);
    return buf;
}

static int medir_faixa(Font *f, const Trecho *t)
{
    int face = face_para_grupo(f, t->grupo, t->amostra);
    hb_buffer_t *buf = shaping(f, face, t->ini, t->bytes);
    unsigned int total = 0;
    hb_glyph_position_t *pos;
    int largura = 0;

    if (!buf) {
        return 0;
    }
    pos = hb_buffer_get_glyph_positions(buf, &total);
    for (unsigned int i = 0; i < total; i++) {
        largura += pos[i].x_advance >> 6;
    }
    hb_buffer_destroy(buf);
    return largura;
}

static int texto_largura(Font *f, const char *texto)
{
    Trecho trechos[TRECHOS_MAX];
    int n = fatiar(texto, trechos, TRECHOS_MAX);
    int largura = 0;

    for (int t = 0; t < n; t++) {
        largura += medir_faixa(f, &trechos[t]);
    }
    return largura;
}

int font_text_width(const Font *f, const char *texto)
{
    if (!f) {
        return 0;
    }
    return texto_largura((Font *) f, texto);
}

static void desenhar_glifo(Canvas *c, const Glifo *g, int x, int y, Color cor)
{
    if (!g || !g->bitmap) {
        return;
    }
    for (int gy = 0; gy < g->h; gy++) {
        int ty = y - g->bearing_y + gy;
        if (ty < 0 || ty >= c->h) {
            continue;
        }
        for (int gx = 0; gx < g->w; gx++) {
            int tx = x + g->bearing_x + gx;
            unsigned char m;

            if (tx < 0 || tx >= c->w) {
                continue;
            }
            m = g->bitmap[(size_t) gy * g->w + gx];
            if (!m) {
                continue;
            }
            {
                uint32_t *dst = &c->px[(size_t) ty * c->w + tx];
                uint32_t a = (uint32_t) m * cor.a / 255;
                uint32_t src = ((uint32_t) cor.r << 16) | ((uint32_t) cor.g << 8) | cor.b;
                uint32_t d = *dst;
                uint32_t inv = 255 - a;
                uint32_t r = (((src >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * inv) / 255;
                uint32_t gg = (((src >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * inv) / 255;
                uint32_t b = ((src & 0xFF) * a + (d & 0xFF) * inv) / 255;
                *dst = (r << 16) | (gg << 8) | b;
            }
        }
    }
}

/* Desenha a partir de `x` e devolve o x final. */
static int desenhar_trechos(Font *f, Canvas *c, int x, int y, Color cor,
                            const char *texto)
{
    Trecho trechos[TRECHOS_MAX];
    int n = fatiar(texto, trechos, TRECHOS_MAX);
    int cursor = x;

    for (int t = 0; t < n; t++) {
        int face = face_para_grupo(f, trechos[t].grupo, trechos[t].amostra);
        hb_buffer_t *buf = shaping(f, face, trechos[t].ini, trechos[t].bytes);
        unsigned int total = 0;
        hb_glyph_info_t *info;
        hb_glyph_position_t *pos;

        if (!buf) {
            continue;
        }
        info = hb_buffer_get_glyph_infos(buf, &total);
        pos = hb_buffer_get_glyph_positions(buf, &total);
        for (unsigned int i = 0; i < total; i++) {
            Glifo *g = glifo_obter(f, face, info[i].codepoint);
            int gx = cursor + (pos[i].x_offset >> 6);
            int gy = y - (pos[i].y_offset >> 6);

            if (g) {
                /* O HarfBuzz devolve os glifos em ordem VISUAL, então desenhar
                 * da esquerda para a direita já sai certo em árabe/urdu. */
                desenhar_glifo(c, g, gx, gy, cor);
            }
            cursor += pos[i].x_advance >> 6;
        }
        hb_buffer_destroy(buf);
    }
    return cursor;
}

/* Quantos bytes (UTF-8) cabem em max_w pixels, sem partir caractere. */
static int bytes_que_cabem(Font *f, const char *texto, int max_w)
{
    int usados = 0;
    int largura = 0;

    while (texto[usados]) {
        Trecho t;
        int largura_trecho;

        if (fatiar(texto + usados, &t, 1) != 1) {
            break;
        }
        largura_trecho = medir_faixa(f, &t);
        if (largura + largura_trecho <= max_w) {
            largura += largura_trecho;
            usados += (int) t.bytes;
            continue;
        }
        /* Não coube inteiro: acrescenta codepoint a codepoint até estourar. */
        while (texto[usados]) {
            char unico[8];
            uint32_t cp;
            int b = wm_utf8_decode(texto + usados, &cp);
            int avanco;

            if (b <= 0 || b >= (int) sizeof(unico)) {
                return usados;
            }
            memcpy(unico, texto + usados, (size_t) b);
            unico[b] = '\0';
            avanco = texto_largura(f, unico);
            if (largura + avanco > max_w) {
                return usados;
            }
            largura += avanco;
            usados += b;
        }
    }
    return usados;
}

int font_draw(Canvas *c, const Font *f, int x, int y, Color cor,
              const char *texto, int max_w)
{
    Font *mut = (Font *) f;
    char cortado[1024];

    if (!f || !texto || cor.a == 0) {
        return 0;
    }
    if (max_w > 0 && texto_largura(mut, texto) > max_w) {
        int reticencias = texto_largura(mut, "...");
        int cabem = bytes_que_cabem(mut, texto, max_w - reticencias);
        size_t n = (size_t) cabem;

        if (n > sizeof(cortado) - 4) {
            n = sizeof(cortado) - 4;
        }
        memcpy(cortado, texto, n);
        /* Corta antes do espaço para não ficar "Titulo ..." */
        while (n > 0 && cortado[n - 1] == ' ') {
            n--;
        }
        snprintf(cortado + n, sizeof(cortado) - n, "...");
        texto = cortado;
    }
    return desenhar_trechos(mut, c, x, y, cor, texto) - x;
}
