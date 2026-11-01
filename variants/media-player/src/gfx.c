/* gfx.c — canvas XRGB8888 desenhado na CPU.
 *
 * Não há aceleradora gráfica em uso: o RK3566 no mainline não tem driver de
 * GPU estável para um app isolado, e o desenho da UI é 2D simples. Tudo aqui é
 * blending em memória, com a mesma estética do protótipo (layout1.md): fundo
 * quase preto, painéis com borda clara, acento vermelho.
 */

#include "wm.h"

#include <math.h>

void canvas_init(Canvas *c, int w, int h)
{
    c->w = w;
    c->h = h;
    c->px = calloc((size_t) w * (size_t) h, sizeof(uint32_t));
    if (!c->px) {
        wm_log("!!! canvas: sem memoria para %dx%d", w, h);
        c->w = c->h = 0;
    }
}

void canvas_free(Canvas *c)
{
    free(c->px);
    c->px = NULL;
    c->w = c->h = 0;
}

void canvas_clear(Canvas *c, Color cor)
{
    uint32_t v = ((uint32_t) cor.r << 16) | ((uint32_t) cor.g << 8) | cor.b;
    size_t total = (size_t) c->w * (size_t) c->h;

    for (size_t i = 0; i < total; i++) {
        c->px[i] = v;
    }
}

static inline uint32_t pack(Color c)
{
    return ((uint32_t) c.r << 16) | ((uint32_t) c.g << 8) | c.b;
}

/* Blend src-over 8 bits, sem tocar no alfa do destino (o canvas é opaco). */
static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t a)
{
    if (a == 0) {
        return dst;
    }
    if (a >= 255) {
        return src;
    }
    {
        uint32_t inv = 255 - a;
        uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
        uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
        uint32_t r = (sr * a + dr * inv + 127) / 255;
        uint32_t g = (sg * a + dg * inv + 127) / 255;
        uint32_t b = (sb * a + db * inv + 127) / 255;
        return (r << 16) | (g << 8) | b;
    }
}

static inline void put(Canvas *c, int x, int y, uint32_t src, uint32_t a)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
        return;
    }
    c->px[(size_t) y * c->w + x] = blend(c->px[(size_t) y * c->w + x], src, a);
}

void gfx_fill(Canvas *c, Rect r, Color cor)
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > c->w ? c->w : r.x + r.w;
    int y1 = r.y + r.h > c->h ? c->h : r.y + r.h;
    uint32_t v = pack(cor);

    if (cor.a == 0 || x1 <= x0 || y1 <= y0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        uint32_t *linha = c->px + (size_t) y * c->w;
        if (cor.a >= 255) {
            for (int x = x0; x < x1; x++) {
                linha[x] = v;
            }
        } else {
            for (int x = x0; x < x1; x++) {
                linha[x] = blend(linha[x], v, cor.a);
            }
        }
    }
}

/* Cobertura de um canto arredondado (0..255), com 1 px de antialias. */
static inline int cobertura_canto(int px, int py, int raio)
{
    int dx = raio - px;
    int dy = raio - py;
    double d;

    if (dx <= 0 || dy <= 0) {
        return 255;
    }
    d = sqrt((double) dx * dx + (double) dy * dy);
    if (d <= raio - 1) {
        return 255;
    }
    if (d >= raio + 0.5) {
        return 0;
    }
    return (int) ((raio + 0.5 - d) * 255.0);
}

/* Cobertura (0..255) de um ponto num retângulo de cantos arredondados. */
static inline int cobertura_round(int x, int y, Rect r, int raio)
{
    int rx = x - r.x;
    int ry = y - r.y;
    int dist_x, dist_y;

    if (rx < 0 || ry < 0 || rx >= r.w || ry >= r.h) {
        return 0;
    }
    dist_x = rx < raio ? rx : (r.w - 1 - rx);
    dist_y = ry < raio ? ry : (r.h - 1 - ry);
    if (dist_x < raio && dist_y < raio) {
        return cobertura_canto(dist_x, dist_y, raio);
    }
    return 255;
}

static inline int raio_limitado(Rect r, int raio)
{
    if (raio > r.w / 2) {
        raio = r.w / 2;
    }
    if (raio > r.h / 2) {
        raio = r.h / 2;
    }
    return raio < 0 ? 0 : raio;
}

void gfx_fill_round(Canvas *c, Rect r, int raio, Color cor)
{
    int x0 = r.x;
    int y0 = r.y;
    int x1 = r.x + r.w;
    int y1 = r.y + r.h;
    uint32_t v = pack(cor);

    if (cor.a == 0 || r.w <= 0 || r.h <= 0) {
        return;
    }
    raio = raio_limitado(r, raio);
    if (raio <= 0) {
        gfx_fill(c, r, cor);
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > c->w) {
        x1 = c->w;
    }
    if (y1 > c->h) {
        y1 = c->h;
    }

    for (int y = y0; y < y1; y++) {
        uint32_t *linha = c->px + (size_t) y * c->w;
        for (int x = x0; x < x1; x++) {
            int cov = cobertura_round(x, y, r, raio);
            if (cov <= 0) {
                continue;
            }
            linha[x] = blend(linha[x], v, (uint32_t) (cov * cor.a / 255));
        }
    }
}

/* Moldura: cobertura do retângulo externo menos a do interno. Assim o anel
 * fica correto em qualquer fundo (não precisa "apagar" o miolo). */
void gfx_stroke_round(Canvas *c, Rect r, int raio, Color cor, int espessura)
{
    Rect dentro;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    uint32_t v = pack(cor);

    if (cor.a == 0 || r.w <= 0 || r.h <= 0 || espessura <= 0) {
        return;
    }
    raio = raio_limitado(r, raio);
    dentro = (Rect) { r.x + espessura, r.y + espessura,
                      r.w - 2 * espessura, r.h - 2 * espessura };
    {
        int raio_in = raio - espessura;
        if (raio_in < 0) {
            raio_in = 0;
        }
        if (dentro.w > 0 && dentro.h > 0) {
            raio_in = raio_limitado(dentro, raio_in);
        }
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (x1 > c->w) {
            x1 = c->w;
        }
        if (y1 > c->h) {
            y1 = c->h;
        }
        for (int y = y0; y < y1; y++) {
            uint32_t *linha = c->px + (size_t) y * c->w;
            for (int x = x0; x < x1; x++) {
                int fora = cobertura_round(x, y, r, raio);
                int cov;
                if (fora <= 0) {
                    continue;
                }
                cov = fora;
                if (dentro.w > 0 && dentro.h > 0) {
                    int d = cobertura_round(x, y, dentro, raio_in);
                    if (d > 0) {
                        cov = fora * (255 - d) / 255;
                    }
                }
                if (cov <= 0) {
                    continue;
                }
                linha[x] = blend(linha[x], v, (uint32_t) (cov * cor.a / 255));
            }
        }
    }
}

void gfx_gradient_v(Canvas *c, Rect r, int raio, Color topo, Color base)
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > c->w ? c->w : r.x + r.w;
    int y1 = r.y + r.h > c->h ? c->h : r.y + r.h;

    if (x1 <= x0 || y1 <= y0 || r.h <= 0) {
        return;
    }
    if (raio > r.w / 2) {
        raio = r.w / 2;
    }
    if (raio > r.h / 2) {
        raio = r.h / 2;
    }
    if (raio < 0) {
        raio = 0;
    }

    for (int y = y0; y < y1; y++) {
        double t = r.h > 1 ? (double) (y - r.y) / (double) (r.h - 1) : 0.0;
        Color cor = {
            (uint8_t) (topo.r + (base.r - topo.r) * t),
            (uint8_t) (topo.g + (base.g - topo.g) * t),
            (uint8_t) (topo.b + (base.b - topo.b) * t),
            (uint8_t) (topo.a + (base.a - topo.a) * t)
        };
        uint32_t v = pack(cor);
        uint32_t *linha = c->px + (size_t) y * c->w;
        int relat_y = y - r.y;
        int dist_y = relat_y < raio ? relat_y : (r.h - 1 - relat_y);

        for (int x = x0; x < x1; x++) {
            int relat_x = x - r.x;
            int dist_x = relat_x < raio ? relat_x : (r.w - 1 - relat_x);
            int cov = 255;
            if (dist_x < raio && dist_y < raio) {
                cov = cobertura_canto(dist_x, dist_y, raio);
                if (cov <= 0) {
                    continue;
                }
            }
            linha[x] = blend(linha[x], v, (uint32_t) (cov * cor.a / 255));
        }
    }
}

void gfx_gradient_h(Canvas *c, Rect r, Color esq, Color dir)
{
    int x0 = r.x < 0 ? 0 : r.x;
    int x1 = r.x + r.w > c->w ? c->w : r.x + r.w;

    if (x1 <= x0 || r.w <= 0) {
        return;
    }
    for (int x = x0; x < x1; x++) {
        double t = (double) (x - r.x) / (double) (r.w - 1);
        Color cor = {
            (uint8_t) (esq.r + (dir.r - esq.r) * t),
            (uint8_t) (esq.g + (dir.g - esq.g) * t),
            (uint8_t) (esq.b + (dir.b - esq.b) * t),
            (uint8_t) (esq.a + (dir.a - esq.a) * t)
        };
        uint32_t v = pack(cor);
        for (int y = r.y; y < r.y + r.h && y < c->h; y++) {
            if (y < 0) {
                continue;
            }
            c->px[(size_t) y * c->w + x] = blend(c->px[(size_t) y * c->w + x], v, cor.a);
        }
    }
}

void gfx_blit(Canvas *c, int x, int y, int w, int h, const uint32_t *src,
              int src_w, int src_h)
{
    if (!src || src_w <= 0 || src_h <= 0 || w <= 0 || h <= 0) {
        return;
    }
    for (int dy = 0; dy < h; dy++) {
        int ty = y + dy;
        int sy;

        if (ty < 0 || ty >= c->h) {
            continue;
        }
        sy = dy * src_h / h;
        if (sy >= src_h) {
            sy = src_h - 1;
        }
        for (int dx = 0; dx < w; dx++) {
            int tx = x + dx;
            int sx;

            if (tx < 0 || tx >= c->w) {
                continue;
            }
            sx = dx * src_w / w;
            if (sx >= src_w) {
                sx = src_w - 1;
            }
            c->px[(size_t) ty * c->w + tx] = src[(size_t) sy * src_w + sx] & 0xFFFFFF;
        }
    }
}

void gfx_blit_round(Canvas *c, Rect r, int raio, const uint32_t *src,
                    int src_w, int src_h)
{
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > c->w ? c->w : r.x + r.w;
    int y1 = r.y + r.h > c->h ? c->h : r.y + r.h;

    if (!src || src_w <= 0 || src_h <= 0) {
        return;
    }
    if (raio > r.w / 2) {
        raio = r.w / 2;
    }
    if (raio > r.h / 2) {
        raio = r.h / 2;
    }
    for (int y = y0; y < y1; y++) {
        int relat_y = y - r.y;
        int dist_y = relat_y < raio ? relat_y : (r.h - 1 - relat_y);
        int sy = relat_y * src_h / (r.h > 0 ? r.h : 1);
        if (sy >= src_h) {
            sy = src_h - 1;
        }
        for (int x = x0; x < x1; x++) {
            int relat_x = x - r.x;
            int dist_x = relat_x < raio ? relat_x : (r.w - 1 - relat_x);
            int cov = 255;
            int sx;

            if (dist_x < raio && dist_y < raio) {
                cov = cobertura_canto(dist_x, dist_y, raio);
                if (cov <= 0) {
                    continue;
                }
            }
            sx = relat_x * src_w / (r.w > 0 ? r.w : 1);
            if (sx >= src_w) {
                sx = src_w - 1;
            }
            {
                uint32_t s = src[(size_t) sy * src_w + sx];
                /* O fonte pode ser opaca; respeita o alfa dela quando houver. */
                uint32_t a = (s >> 24) & 0xFF;
                if (a == 0) {
                    a = 255;
                }
                put(c, x, y, s & 0xFFFFFF, (uint32_t) (a * cov / 255));
            }
        }
    }
}

void gfx_shadow_bottom(Canvas *c, Rect r, int altura, uint8_t alfa_max)
{
    for (int i = 0; i < altura; i++) {
        uint8_t a = (uint8_t) (alfa_max - (alfa_max * i) / (altura > 0 ? altura : 1));
        gfx_fill(c, (Rect) { r.x, r.y + r.h + i, r.w, 1 }, wm_rgba(0, 0, 0, a));
    }
}
