/* font.c — texto na CPU via FreeType.
 *
 * Cada glifo é rasterizado uma vez para um bitmap de cobertura 8 bits e fica
 * no cache; desenhar texto é, então, só blending de máscaras. Isso importa
 * porque a UI inteira (listas, OSD, relógio) é redesenhada a cada quadro e não
 * existe GPU em uso.
 */

#include "wm.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define GLIFOS_MAX 512

typedef struct {
    uint32_t codigo;
    int w, h;
    int bearing_x, bearing_y;
    int avanco;
    uint8_t *bitmap;
} Glifo;

struct Font {
    FT_Face face;
    int tamanho;
    int altura_linha;
    int ascent;
    Glifo cache[GLIFOS_MAX];
    int n_cache;
};

static FT_Library g_ft;
static int g_ft_refs;

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

Font *font_open(const char *path, int tamanho_px)
{
    Font *f;
    FT_Face face;

    if (!path || tamanho_px <= 0) {
        return NULL;
    }
    if (!ft_garantir()) {
        return NULL;
    }
    if (FT_New_Face(g_ft, path, 0, &face) != 0) {
        wm_log("fonte nao encontrada: %s", path);
        g_ft_refs--;
        return NULL;
    }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt) tamanho_px);

    f = calloc(1, sizeof(*f));
    if (!f) {
        FT_Done_Face(face);
        return NULL;
    }
    f->face = face;
    f->tamanho = tamanho_px;
    /* Altura de linha = altura da fonte + folga; ascent vem em 26.6. */
    f->altura_linha = (int) ((face->size->metrics.height + 63) >> 6);
    f->ascent = (int) ((face->size->metrics.ascender + 63) >> 6);
    if (f->altura_linha <= 0) {
        f->altura_linha = tamanho_px + 2;
    }
    return f;
}

void font_close(Font *f)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < f->n_cache; i++) {
        free(f->cache[i].bitmap);
    }
    if (f->face) {
        FT_Done_Face(f->face);
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

static Glifo *glifo_obter(Font *f, uint32_t codigo)
{
    FT_Face face = f->face;
    FT_UInt indice;
    Glifo *g;

    for (int i = 0; i < f->n_cache; i++) {
        if (f->cache[i].codigo == codigo) {
            return &f->cache[i];
        }
    }
    if (f->n_cache >= GLIFOS_MAX) {
        /* Cache cheio: reaproveita a entrada mais antiga (rotação simples). */
        g = &f->cache[0];
        free(g->bitmap);
        memmove(&f->cache[0], &f->cache[1], sizeof(Glifo) * (GLIFOS_MAX - 1));
        g = &f->cache[GLIFOS_MAX - 1];
    } else {
        g = &f->cache[f->n_cache++];
    }
    memset(g, 0, sizeof(*g));
    g->codigo = codigo;

    indice = FT_Get_Char_Index(face, codigo);
    if (indice == 0) {
        indice = FT_Get_Char_Index(face, '?');
    }
    if (indice == 0 || FT_Load_Glyph(face, indice, FT_LOAD_RENDER) != 0) {
        return NULL;
    }
    {
        FT_GlyphSlot slot = face->glyph;
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

static int texto_largura(Font *f, const char *texto)
{
    int largura = 0;

    while (texto && *texto) {
        uint32_t cp;
        int n = wm_utf8_decode(texto, &cp);
        Glifo *g;

        if (n <= 0) {
            break;
        }
        texto += n;
        g = glifo_obter(f, cp);
        largura += g ? g->avanco : f->tamanho / 2;
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

/* Quantos bytes (de UTF-8) cabem em max_w pixels. */
static int bytes_que_cabem(Font *f, const char *texto, int max_w)
{
    int largura = 0;
    int usados = 0;

    while (*texto) {
        uint32_t cp;
        int n = wm_utf8_decode(texto, &cp);
        Glifo *g = glifo_obter(f, cp);
        int avanco = g ? g->avanco : f->tamanho / 2;

        if (largura + avanco > max_w) {
            break;
        }
        largura += avanco;
        usados += n;
        texto += n;
    }
    return usados;
}

int font_draw(Canvas *c, const Font *f, int x, int y, Color cor,
              const char *texto, int max_w)
{
    int cursor = x;
    Font *mut = (Font *) f;

    if (!f || !texto || cor.a == 0) {
        return 0;
    }
    if (max_w > 0 && texto_largura(mut, texto) > max_w) {
        int cabem = bytes_que_cabem(mut, texto, max_w - texto_largura(mut, "..."));
        char cortado[1024];
        size_t n = (size_t) cabem;

        if (n > sizeof(cortado) - 4) {
            n = sizeof(cortado) - 4;
        }
        memcpy(cortado, texto, n);
        snprintf(cortado + n, sizeof(cortado) - n, "...");
        texto = cortado;
    }
    while (*texto) {
        uint32_t cp;
        int n = wm_utf8_decode(texto, &cp);
        Glifo *g;

        if (n <= 0) {
            break;
        }
        texto += n;
        g = glifo_obter(mut, cp);
        if (g) {
            desenhar_glifo(c, g, cursor, y, cor);
            cursor += g->avanco;
        }
    }
    return cursor - x;
}
