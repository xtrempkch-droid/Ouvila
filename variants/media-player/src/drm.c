/* drm.c — saída de vídeo direto no KMS (sem compositor).
 *
 * Por que DRM direto: nas variantes anteriores um compositor (Weston) aplicava
 * a rotação e o app só desenhava. Aqui o app é dono do dispositivo de vídeo:
 * pega o CRTC, cria um dumb buffer do tamanho do painel, desenha na CPU e faz
 * page flip esperando o vblank. A rotação é aplicada NA CÓPIA.
 *
 * Rotação: a X55 tem painel 720x1280 montado deitado (rotate-90); a X35S/X35H
 * tem 640x480 e fica em normal. O valor vem de display_transform em
 * /boot/ouvila.conf ("auto" decide pelo /proc/device-tree/model).
 *
 * O canvas tem o tamanho JÁ GIRADO (1280x720 na X55). Este arquivo é o único
 * lugar que sabe que o framebuffer é 720x1280.
 */

#include "wm.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/kd.h>
#include <linux/vt.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct Drm {
    int fd;
    uint32_t conn_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint32_t size;
    void *map;

    /* Estado original, restaurado ao sair (senão o console fica preto). */
    drmModeCrtc *crtc_orig;

    int fb_w, fb_h;      /* painel nativo */
    int ui_w, ui_h;      /* canvas depois da rotação */
    int rotacao;         /* 0, 90, 180, 270 */
    bool flip_x;
    int tty_fd;
    bool tem_flip_pendente;
    volatile bool flip_concluido;
    int falhas;
    int dpms_prop;      /* id da propriedade DPMS do conector (-1 = não existe) */
    bool dpms_ligado;
};

static void parse_transform(const char *nome, int *rotacao, bool *flip_x)
{
    *rotacao = 0;
    *flip_x = false;
    if (!nome) {
        return;
    }
    if (strstr(nome, "flipped")) {
        *flip_x = true;
    }
    if (strstr(nome, "90") && strstr(nome, "270")) {
        *rotacao = 0;
    } else if (strstr(nome, "270")) {
        *rotacao = 270;
    } else if (strstr(nome, "90")) {
        *rotacao = 90;
    } else if (strstr(nome, "180")) {
        *rotacao = 180;
    }
}

static void flush_handler(int fd, unsigned int frame, unsigned int sec,
                          unsigned int usec, void *data)
{
    (void) fd;
    (void) frame;
    (void) sec;
    (void) usec;
    {
        Drm *d = data;
        d->flip_concluido = true;
        d->tem_flip_pendente = false;
    }
}

/* Tira o console de texto do caminho: com o fbcon desenhando no mesmo CRTC, o
 * kernel continua pintando por cima do nosso framebuffer (a tela pisca com o
 * log do systemd) e o page flip briga com o fbcon. KD_GRAPHICS + ativar o VT
 * para o fbcon; é o que faz apps KMS puros (kmscube e afins).
 * Devolve o fd do console (ou -1) para restaurar ao sair. */
static int console_para_grafico(void)
{
    int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    int tty = 1;

    if (fd < 0) {
        fd = open("/dev/console", O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) {
        wm_log("drm: sem /dev/tty0; seguindo sem mudar o modo do console");
        return -1;
    }
    ioctl(fd, VT_ACTIVATE, tty);
    ioctl(fd, VT_WAITACTIVE, tty);
    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) != 0) {
        wm_log("drm: KDSETMODE(KD_GRAPHICS) falhou: %s", strerror(errno));
    }
    return fd;
}

static void console_restaurar(int fd)
{
    if (fd < 0) {
        return;
    }
    ioctl(fd, KDSETMODE, KD_TEXT);
    close(fd);
}

Drm *drm_open(struct DrmInfo *info, const char *transform)
{
    Drm *d = calloc(1, sizeof(*d));
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;
    drmModeEncoder *enc = NULL;
    const char *caminhos[] = { "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2" };

    if (!d) {
        return NULL;
    }
    d->fd = -1;
    d->tty_fd = console_para_grafico();
    for (size_t i = 0; i < sizeof(caminhos) / sizeof(caminhos[0]); i++) {
        d->fd = open(caminhos[i], O_RDWR | O_CLOEXEC);
        if (d->fd >= 0) {
            wm_log("drm: aberto %s", caminhos[i]);
            break;
        }
    }
    if (d->fd < 0) {
        wm_log("!!! drm: nenhum /dev/dri/card* (o driver rockchip-drm esta carregado?)");
        goto falha;
    }
    if (drmSetMaster(d->fd) != 0) {
        wm_log("drm: drmSetMaster falhou (%s); seguindo como cliente", strerror(errno));
    }
    drmDropMaster(d->fd); /* devolve o master e retoma abaixo: garante limpeza */
    drmSetMaster(d->fd);

    res = drmModeGetResources(d->fd);
    if (!res) {
        wm_log("!!! drm: sem recursos KMS: %s", strerror(errno));
        goto falha;
    }

    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(d->fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
        } else if (c) {
            drmModeFreeConnector(c);
        }
    }
    if (!conn) {
        wm_log("!!! drm: nenhum conector conectado");
        goto falha;
    }
    d->conn_id = conn->connector_id;

    /* Modo preferido do painel; se não houver, o primeiro da lista. */
    d->mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            d->mode = conn->modes[i];
            break;
        }
    }
    d->fb_w = (int) d->mode.hdisplay;
    d->fb_h = (int) d->mode.vdisplay;
    d->dpms_prop = -1;
    d->dpms_ligado = true;

    /* Propriedade DPMS do conector: é o que apaga a saída de verdade (o painel
     * para de ser varrido e o backlight pode ser desligado pelo kernel). Usado
     * pelo "apagar a tela" do botão de power. */
    for (int i = 0; i < conn->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(d->fd, conn->props[i]);
        if (!prop) {
            continue;
        }
        if (strcmp(prop->name, "DPMS") == 0) {
            d->dpms_prop = (int) prop->prop_id;
        }
        drmModeFreeProperty(prop);
        if (d->dpms_prop >= 0) {
            break;
        }
    }

    if (conn->encoder_id) {
        enc = drmModeGetEncoder(d->fd, conn->encoder_id);
    }
    if (enc && enc->crtc_id) {
        d->crtc_id = enc->crtc_id;
    } else {
        /* Conector sem encoder ativo: pega o primeiro encoder possível. */
        for (int i = 0; i < conn->count_encoders && !d->crtc_id; i++) {
            drmModeEncoder *e = drmModeGetEncoder(d->fd, conn->encoders[i]);
            if (!e) {
                continue;
            }
            for (int j = 0; j < res->count_crtcs; j++) {
                if (e->possible_crtcs & (1 << j)) {
                    d->crtc_id = (uint32_t) res->crtcs[j];
                    break;
                }
            }
            drmModeFreeEncoder(e);
        }
    }
    if (!d->crtc_id) {
        wm_log("!!! drm: nenhum CRTC disponivel para o conector %u", d->conn_id);
        goto falha;
    }

    d->crtc_orig = drmModeGetCrtc(d->fd, d->crtc_id);

    parse_transform(transform, &d->rotacao, &d->flip_x);
    if (d->rotacao == 90 || d->rotacao == 270) {
        d->ui_w = d->fb_h;
        d->ui_h = d->fb_w;
    } else {
        d->ui_w = d->fb_w;
        d->ui_h = d->fb_h;
    }

    {
        struct drm_mode_create_dumb criar;
        memset(&criar, 0, sizeof(criar));
        criar.width = (uint32_t) d->fb_w;
        criar.height = (uint32_t) d->fb_h;
        criar.bpp = 32;
        criar.flags = 0;
        if (drmIoctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &criar) != 0) {
            wm_log("!!! drm: DRM_IOCTL_MODE_CREATE_DUMB falhou: %s", strerror(errno));
            goto falha;
        }
        d->handle = criar.handle;
        d->pitch = criar.pitch;
        d->size = criar.size;
    }
    if (drmModeAddFB(d->fd, (uint32_t) d->fb_w, (uint32_t) d->fb_h, 24, 32,
                     d->pitch, d->handle, &d->fb_id) != 0) {
        wm_log("!!! drm: drmModeAddFB falhou: %s", strerror(errno));
        goto falha;
    }
    {
        struct drm_mode_map_dumb mapear;
        memset(&mapear, 0, sizeof(mapear));
        mapear.handle = d->handle;
        if (drmIoctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mapear) != 0) {
            wm_log("!!! drm: DRM_IOCTL_MODE_MAP_DUMB falhou: %s", strerror(errno));
            goto falha;
        }
        d->map = mmap(NULL, d->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      d->fd, (off_t) mapear.offset);
        if (d->map == MAP_FAILED) {
            d->map = NULL;
            wm_log("!!! drm: mmap do framebuffer falhou: %s", strerror(errno));
            goto falha;
        }
    }

    /* Primeiro commit: sem ele o page flip não tem "anterior" para trocar. */
    memset(d->map, 0, d->size);
    if (drmModeSetCrtc(d->fd, d->crtc_id, d->fb_id, 0, 0, &d->conn_id, 1,
                       &d->mode) != 0) {
        wm_log("!!! drm: drmModeSetCrtc falhou: %s", strerror(errno));
        goto falha;
    }

    if (info) {
        memset(info, 0, sizeof(*info));
        info->fb_w = d->fb_w;
        info->fb_h = d->fb_h;
        info->ui_w = d->ui_w;
        info->ui_h = d->ui_h;
        snprintf(info->connector, sizeof(info->connector), "tipo%u-%u",
                 conn->connector_type, conn->connector_type_id);
        snprintf(info->mode, sizeof(info->mode), "%ux%u@%.1f", d->mode.hdisplay,
                 d->mode.vdisplay, (double) d->mode.vrefresh);
    }
    wm_log("drm: painel %dx%d, UI %dx%d, rotacao %d%s", d->fb_w, d->fb_h,
           d->ui_w, d->ui_h, d->rotacao, d->flip_x ? " + espelho" : "");

    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return d;

falha:
    if (enc) {
        drmModeFreeEncoder(enc);
    }
    if (conn) {
        drmModeFreeConnector(conn);
    }
    if (res) {
        drmModeFreeResources(res);
    }
    drm_close(d);
    return NULL;
}

/* Cópia com rotação, em blocos de 32x32 (sem isso, ler uma coluna do canvas
 * custa uma linha de cache por pixel).
 *
 * Mapa destino -> origem (dx,dy no framebuffer; sx,sy no canvas):
 *   0°    sx=dx,        sy=dy
 *   180°  sx=sw-1-dx,   sy=sh-1-dy
 *   90°   sx=dy,        sy=sh-1-dx     (giro horário;  dw=sh, dh=sw)
 *   270°  sx=sw-1-dy,   sy=dx          (giro anti-horário; dw=sh, dh=sw)
 * Espelho horizontal (transforms "flipped*") = usar dw-1-dx no lugar de dx. */
static void copiar_rotacionado(Drm *d, const Canvas *c)
{
    uint32_t *dst = d->map;
    const uint32_t *src = c->px;
    const int dst_pitch = (int) (d->pitch / 4);
    const int sw = c->w;
    const int sh = c->h;
    const int dw = (d->rotacao == 90 || d->rotacao == 270) ? sh : sw;
    const int dh = (d->rotacao == 90 || d->rotacao == 270) ? sw : sh;

    for (int bloco_y = 0; bloco_y < dh; bloco_y += 32) {
        int y_fim = bloco_y + 32 < dh ? bloco_y + 32 : dh;
        for (int bloco_x = 0; bloco_x < dw; bloco_x += 32) {
            int x_fim = bloco_x + 32 < dw ? bloco_x + 32 : dw;

            for (int dy = bloco_y; dy < y_fim; dy++) {
                uint32_t *linha_dst = dst + (size_t) dy * dst_pitch;
                for (int dx = bloco_x; dx < x_fim; dx++) {
                    int mdx = d->flip_x ? (dw - 1 - dx) : dx;
                    int sx, sy;

                    switch (d->rotacao) {
                    case 90:
                        sx = dy;
                        sy = sh - 1 - mdx;
                        break;
                    case 180:
                        sx = sw - 1 - mdx;
                        sy = sh - 1 - dy;
                        break;
                    case 270:
                        sx = sw - 1 - dy;
                        sy = mdx;
                        break;
                    default:
                        sx = mdx;
                        sy = dy;
                        break;
                    }
                    if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) {
                        continue;
                    }
                    linha_dst[dx] = src[(size_t) sy * sw + sx] & 0xFFFFFF;
                }
            }
        }
    }
}

bool drm_dpms(Drm *d, bool ligado)
{
    if (!d || d->dpms_prop < 0) {
        return false;
    }
    if (d->dpms_ligado == ligado) {
        return true;
    }
    /* 0 = On, 3 = Off (enum DRM_MODE_DPMS_ON/OFF). */
    if (drmModeConnectorSetProperty(d->fd, d->conn_id, (uint32_t) d->dpms_prop,
                                    ligado ? DRM_MODE_DPMS_ON
                                           : DRM_MODE_DPMS_OFF) != 0) {
        wm_log("drm: DPMS %s falhou: %s", ligado ? "on" : "off", strerror(errno));
        return false;
    }
    d->dpms_ligado = ligado;
    wm_log("drm: saida %s (DPMS)", ligado ? "ligada" : "desligada");
    return true;
}

int drm_falhas(const Drm *d)
{
    return d ? d->falhas : 0;
}

bool drm_present(Drm *d, const Canvas *c)
{
    struct pollfd pfd;
    drmEventContext ev;

    if (!d || !c || !c->px || !d->map) {
        return false;
    }
    copiar_rotacionado(d, c);

    /* Um flip por vez: com flip pendente, espera ele terminar. */
    if (d->tem_flip_pendente) {
        pfd.fd = d->fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) > 0) {
            memset(&ev, 0, sizeof(ev));
            ev.version = DRM_EVENT_CONTEXT_VERSION;
            ev.page_flip_handler = flush_handler;
            drmHandleEvent(d->fd, &ev);
        }
        d->tem_flip_pendente = false;
    }

    d->flip_concluido = false;
    if (drmModePageFlip(d->fd, d->crtc_id, d->fb_id, DRM_MODE_PAGE_FLIP_EVENT, d) != 0) {
        /* Alguns drivers recusam page flip no primeiro frame; cai para SetCrtc. */
        if (drmModeSetCrtc(d->fd, d->crtc_id, d->fb_id, 0, 0, &d->conn_id, 1,
                           &d->mode) != 0) {
            /* Só loga a primeira falha de cada rajada: se o DRM morrer, isso
             * aconteceria a cada quadro e encheria o log do cartão. */
            if (d->falhas++ % 120 == 0) {
                wm_log("drm: page flip e SetCrtc falharam: %s", strerror(errno));
            }
            return false;
        }
        return true;
    }
    d->tem_flip_pendente = true;

    /* Espera o vblank (timeout curto: se o driver não avisar, não travamos). */
    pfd.fd = d->fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 100) > 0) {
        memset(&ev, 0, sizeof(ev));
        ev.version = DRM_EVENT_CONTEXT_VERSION;
        ev.page_flip_handler = flush_handler;
        drmHandleEvent(d->fd, &ev);
    }
    d->tem_flip_pendente = false;
    return true;
}

void drm_close(Drm *d)
{
    if (!d) {
        return;
    }
    if (d->fd >= 0) {
        if (d->crtc_orig) {
            drmModeSetCrtc(d->fd, d->crtc_orig->crtc_id, d->crtc_orig->buffer_id,
                           d->crtc_orig->x, d->crtc_orig->y, &d->conn_id, 1,
                           &d->crtc_orig->mode);
        }
        if (d->map && d->map != MAP_FAILED) {
            munmap(d->map, d->size);
        }
        if (d->fb_id) {
            drmModeRmFB(d->fd, d->fb_id);
        }
        if (d->handle) {
            struct drm_mode_destroy_dumb destruir;
            memset(&destruir, 0, sizeof(destruir));
            destruir.handle = d->handle;
            drmIoctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destruir);
        }
        drmDropMaster(d->fd);
        close(d->fd);
    }
    console_restaurar(d->tty_fd);
    if (d->crtc_orig) {
        drmModeFreeCrtc(d->crtc_orig);
    }
    free(d);
}
