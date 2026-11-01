/* library.c — biblioteca de mídia.
 *
 * Varre os diretórios configurados em segundo plano (a UI nunca espera o
 * disco) e alimenta a lista de músicas/vídeos. As tags são lidas diretamente
 * do arquivo: ID3v2 (TIT2/TPE1/TALB), ID3v1 e o bloco VORBIS_COMMENT do FLAC.
 * O FFmpeg também sabe fazer isso, mas abrir cada arquivo com o libavformat
 * custa dezenas de ms — numa pasta com 500 músicas isso travaria a varredura.
 */

#include "wm.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARQUIVOS_MAX 4000

static const char *EXT_AUDIO[] = {
    "mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "wma", "ape",
    "wv", "m4b", "aiff", "aif", "mod", "xm", "s3m", "it", NULL
};

static const char *EXT_VIDEO[] = {
    "mp4", "mkv", "avi", "webm", "mov", "m4v", "mpg", "mpeg", "ts", "m2ts",
    "wmv", "flv", "ogv", "3gp", "vob", "divx", NULL
};

static const char *COVER_NAMES[] = { "cover", "folder", "front", "album", "capa", NULL };
static const char *COVER_EXT[] = { "jpg", "jpeg", "png", "webp", NULL };

struct Library {
    char raizes[8][WM_PATH_MAX];
    int n_raizes;

    pthread_mutex_t lock;
    pthread_t thread;
    bool rodando;
    bool pronto;
    unsigned versao;

    struct MediaItem *itens;
    int n_itens;
    int cap_itens;
};

static const char *extensao(const char *nome)
{
    const char *ponto = strrchr(nome, '.');
    return ponto && ponto[1] ? ponto + 1 : "";
}

static int casar_extensao(const char *ext, const char **lista)
{
    for (int i = 0; lista[i]; i++) {
        if (wm_casecmp(ext, lista[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- tags ------------------------------------------------------------- */

static uint32_t syncsafe32(const unsigned char *b)
{
    return ((uint32_t) (b[0] & 0x7F) << 21) | ((uint32_t) (b[1] & 0x7F) << 14)
        | ((uint32_t) (b[2] & 0x7F) << 7) | (uint32_t) (b[3] & 0x7F);
}

static void texto_id3(const unsigned char *dados, size_t n, char *out, size_t tam)
{
    unsigned char enc;
    size_t i = 0, j = 0;

    if (n < 2) {
        return;
    }
    enc = dados[0];
    if (enc == 0 || enc == 3) {
        /* latin-1 / utf-8: copia até o primeiro byte nulo */
        for (i = 1; i < n && j + 1 < tam; i++) {
            unsigned char c = dados[i];
            if (c == 0) {
                break;
            }
            /* UTF-8 pode ter multibyte; em latin-1 é só um byte. */
            out[j++] = (char) c;
        }
    } else {
        /* UTF-16 com/sem BOM: converte para ASCII descartando os nulos altos. */
        bool big = (enc == 2);
        i = 1;
        if (n > 3 && ((dados[1] == 0xFF && dados[2] == 0xFE) || (dados[1] == 0xFE && dados[2] == 0xFF))) {
            big = (dados[1] == 0xFE);
            i = 3;
        }
        for (; i + 1 < n && j + 1 < tam; i += 2) {
            unsigned int c = big ? (unsigned int) ((dados[i] << 8) | dados[i + 1])
                                 : (unsigned int) (dados[i] | (dados[i + 1] << 8));
            if (c == 0) {
                break;
            }
            out[j++] = (c < 128) ? (char) c : '?';
        }
    }
    out[j] = '\0';
    wm_trim(out);
}

static void ler_id3v2(const char *caminho, struct MediaItem *item)
{
    unsigned char cab[10];
    unsigned char *corpo;
    uint32_t tamanho, pos;
    int versao, flags;
    FILE *f = fopen(caminho, "rb");

    if (!f) {
        return;
    }
    if (fread(cab, 1, 10, f) != 10 || memcmp(cab, "ID3", 3) != 0) {
        fclose(f);
        return;
    }
    versao = cab[3];
    flags = cab[5];
    tamanho = syncsafe32(cab + 6);
    if (tamanho == 0 || tamanho > 4 * 1024 * 1024) {
        fclose(f);
        return;
    }
    corpo = malloc(tamanho);
    if (!corpo) {
        fclose(f);
        return;
    }
    if (fread(corpo, 1, tamanho, f) != tamanho) {
        free(corpo);
        fclose(f);
        return;
    }
    fclose(f);

    pos = 0;
    if (flags & 0x40) {
        /* Cabeçalho estendido: 4 bytes de tamanho (syncsafe no v2.4). */
        if (tamanho < 4) {
            free(corpo);
            return;
        }
        pos = (versao >= 4) ? syncsafe32(corpo) : ((uint32_t) corpo[0] << 24 | corpo[1] << 16 | corpo[2] << 8 | corpo[3]);
        pos += (versao >= 4) ? 0 : 4;
    }

    while (pos + 10 <= tamanho) {
        char id[5] = { 0 };
        uint32_t tam;
        size_t cabecalho = (versao == 2) ? 6 : 10;

        memcpy(id, corpo + pos, (versao == 2) ? 3 : 4);
        if ((unsigned char) id[0] == 0) {
            break;
        }
        if (versao == 2) {
            tam = ((uint32_t) corpo[pos + 3] << 16) | ((uint32_t) corpo[pos + 4] << 8) | corpo[pos + 5];
        } else if (versao >= 4) {
            tam = syncsafe32(corpo + pos + 4);
        } else {
            tam = ((uint32_t) corpo[pos + 4] << 24) | ((uint32_t) corpo[pos + 5] << 16)
                | ((uint32_t) corpo[pos + 6] << 8) | corpo[pos + 7];
        }
        if (tam == 0 || pos + cabecalho + tam > tamanho) {
            break;
        }
        if (strcmp(id, "TIT2") == 0 || strcmp(id, "TT2") == 0) {
            texto_id3(corpo + pos + cabecalho, tam, item->titulo, sizeof(item->titulo));
        } else if (strcmp(id, "TPE1") == 0 || strcmp(id, "TP1") == 0) {
            texto_id3(corpo + pos + cabecalho, tam, item->artista, sizeof(item->artista));
        } else if (strcmp(id, "TALB") == 0 || strcmp(id, "TAL") == 0) {
            texto_id3(corpo + pos + cabecalho, tam, item->album, sizeof(item->album));
        }
        pos += cabecalho + tam;
    }
    free(corpo);
}

static void ler_id3v1(const char *caminho, struct MediaItem *item)
{
    FILE *f = fopen(caminho, "rb");
    unsigned char buf[128];

    if (!f) {
        return;
    }
    if (fseek(f, -128, SEEK_END) != 0 || fread(buf, 1, 128, f) != 128
        || memcmp(buf, "TAG", 3) != 0) {
        fclose(f);
        return;
    }
    fclose(f);
    {
        char tmp[32];
        size_t i;
        if (!item->titulo[0]) {
            for (i = 0; i < 30 && buf[3 + i]; i++) {
                tmp[i] = (char) buf[3 + i];
            }
            tmp[i] = '\0';
            wm_trim(tmp);
            snprintf(item->titulo, sizeof(item->titulo), "%s", tmp);
        }
        if (!item->artista[0]) {
            for (i = 0; i < 30 && buf[33 + i]; i++) {
                tmp[i] = (char) buf[33 + i];
            }
            tmp[i] = '\0';
            wm_trim(tmp);
            snprintf(item->artista, sizeof(item->artista), "%s", tmp);
        }
        if (!item->album[0]) {
            for (i = 0; i < 30 && buf[63 + i]; i++) {
                tmp[i] = (char) buf[63 + i];
            }
            tmp[i] = '\0';
            wm_trim(tmp);
            snprintf(item->album, sizeof(item->album), "%s", tmp);
        }
    }
}

static void ler_flac(const char *caminho, struct MediaItem *item)
{
    FILE *f = fopen(caminho, "rb");
    unsigned char cab[4];

    if (!f) {
        return;
    }
    if (fread(cab, 1, 4, f) != 4 || memcmp(cab, "fLaC", 4) != 0) {
        fclose(f);
        return;
    }
    for (int bloco = 0; bloco < 32; bloco++) {
        unsigned char h[4];
        int tipo, ultimo;
        uint32_t tam;

        if (fread(h, 1, 4, f) != 4) {
            break;
        }
        ultimo = h[0] & 0x80;
        tipo = h[0] & 0x7F;
        tam = ((uint32_t) h[1] << 16) | ((uint32_t) h[2] << 8) | h[3];
        if (tipo == 4 && tam > 0 && tam < 1024 * 1024) {
            unsigned char *d = malloc(tam);
            if (d && fread(d, 1, tam, f) == tam && tam > 8) {
                uint32_t fornecedor = (uint32_t) d[0] | ((uint32_t) d[1] << 8)
                    | ((uint32_t) d[2] << 16) | ((uint32_t) d[3] << 24);
                uint32_t pos = 4 + fornecedor;
                if (pos + 4 <= tam) {
                    uint32_t n = (uint32_t) d[pos] | ((uint32_t) d[pos + 1] << 8)
                        | ((uint32_t) d[pos + 2] << 16) | ((uint32_t) d[pos + 3] << 24);
                    pos += 4;
                    for (uint32_t i = 0; i < n && pos + 4 <= tam; i++) {
                        uint32_t len = (uint32_t) d[pos] | ((uint32_t) d[pos + 1] << 8)
                            | ((uint32_t) d[pos + 2] << 16) | ((uint32_t) d[pos + 3] << 24);
                        char entrada[512];
                        size_t copia = len < sizeof(entrada) - 1 ? len : sizeof(entrada) - 1;
                        pos += 4;
                        if (pos + len > tam) {
                            break;
                        }
                        memcpy(entrada, d + pos, copia);
                        entrada[copia] = '\0';
                        pos += len;
                        {
                            char *igual = strchr(entrada, '=');
                            if (!igual) {
                                continue;
                            }
                            *igual = '\0';
                            if (wm_casecmp(entrada, "TITLE") == 0 && !item->titulo[0]) {
                                snprintf(item->titulo, sizeof(item->titulo), "%s", igual + 1);
                            } else if (wm_casecmp(entrada, "ARTIST") == 0 && !item->artista[0]) {
                                snprintf(item->artista, sizeof(item->artista), "%s", igual + 1);
                            } else if (wm_casecmp(entrada, "ALBUM") == 0 && !item->album[0]) {
                                snprintf(item->album, sizeof(item->album), "%s", igual + 1);
                            }
                        }
                    }
                }
            }
            free(d);
            break;
        }
        if (fseek(f, (long) tam, SEEK_CUR) != 0) {
            break;
        }
        if (ultimo) {
            break;
        }
    }
    fclose(f);
}

/* "Artista - Título.flac" quando não há tags. */
static void nome_como_titulo(const char *nome_arquivo, struct MediaItem *item)
{
    char copia[256];
    char *ponto, *sep;

    snprintf(copia, sizeof(copia), "%s", nome_arquivo);
    ponto = strrchr(copia, '.');
    if (ponto && ponto != copia) {
        *ponto = '\0';
    }
    {
        char *p = copia;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        if (p != copia && (*p == ' ' || *p == '-' || *p == '_' || *p == '.')) {
            while (*p == ' ' || *p == '-' || *p == '_' || *p == '.') {
                p++;
            }
            memmove(copia, p, strlen(p) + 1);
        }
    }
    sep = strstr(copia, " - ");
    if (sep) {
        *sep = '\0';
        if (!item->artista[0]) {
            snprintf(item->artista, sizeof(item->artista), "%.*s",
                     (int) sizeof(item->artista) - 1, copia);
            wm_trim(item->artista);
        }
        snprintf(item->titulo, sizeof(item->titulo), "%.*s",
                 (int) sizeof(item->titulo) - 1, sep + 3);
    } else {
        snprintf(item->titulo, sizeof(item->titulo), "%.*s",
                 (int) sizeof(item->titulo) - 1, copia);
    }
    wm_trim(item->titulo);
}

static bool tem_capa(const char *diretorio)
{
    DIR *d = opendir(diretorio);
    struct dirent *e;
    bool achou = false;

    if (!d) {
        return false;
    }
    while (!achou && (e = readdir(d))) {
        char base[256];
        char *ponto;

        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(base, sizeof(base), "%s", e->d_name);
        ponto = strrchr(base, '.');
        if (!ponto) {
            continue;
        }
        *ponto = '\0';
        if (!casar_extensao(ponto + 1, COVER_EXT)) {
            continue;
        }
        for (int i = 0; COVER_NAMES[i]; i++) {
            if (wm_casecmp(base, COVER_NAMES[i]) == 0) {
                achou = true;
                break;
            }
        }
    }
    closedir(d);
    return achou;
}

/* ---- varredura -------------------------------------------------------- */

static void adicionar(Library *lib, const char *caminho, const char *nome,
                      int tipo, const struct stat *st)
{
    struct MediaItem *item;

    if (lib->n_itens >= ARQUIVOS_MAX) {
        return;
    }
    if (lib->n_itens == lib->cap_itens) {
        int novo = lib->cap_itens ? lib->cap_itens * 2 : 64;
        struct MediaItem *tmp = realloc(lib->itens, (size_t) novo * sizeof(*tmp));
        if (!tmp) {
            return;
        }
        lib->itens = tmp;
        lib->cap_itens = novo;
    }
    item = &lib->itens[lib->n_itens];
    memset(item, 0, sizeof(*item));
    snprintf(item->caminho, sizeof(item->caminho), "%.*s",
             (int) sizeof(item->caminho) - 1, caminho);
    item->tipo = tipo;
    item->tamanho = (int64_t) st->st_size;

    {
        /* diretório, para procurar capa uma vez só */
        char dir[WM_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", caminho);
        {
            char *barra = strrchr(dir, '/');
            if (barra) {
                *barra = '\0';
            }
        }
        item->tem_capa = tem_capa(dir);
    }

    if (tipo == WM_MIDIA_AUDIO) {
        const char *ext = extensao(nome);
        if (casar_extensao(ext, (const char *[]) { "flac", NULL })) {
            ler_flac(caminho, item);
        }
        if (casar_extensao(ext, (const char *[]) { "mp3", "m4a", "aac", "wma", "aiff", NULL })) {
            ler_id3v2(caminho, item);
            if (!item->titulo[0] || !item->artista[0]) {
                ler_id3v1(caminho, item);
            }
        }
        if (!item->titulo[0]) {
            nome_como_titulo(nome, item);
        }
    } else {
        nome_como_titulo(nome, item);
    }
    lib->n_itens++;
}

/* Varre um diretório e (até 4 níveis) os subdiretórios. A biblioteca é
 * organizada por pasta (music/, videos/, <artista>/<álbum>/), então recursão
 * não é opcional. */
static void varrer_dir(Library *lib, const char *dir, int nivel)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d || nivel > 4 || lib->n_itens >= ARQUIVOS_MAX) {
        if (d) {
            closedir(d);
        }
        return;
    }
    while ((e = readdir(d)) && lib->n_itens < ARQUIVOS_MAX) {
        char caminho[WM_PATH_MAX];
        struct stat st;
        const char *ext;
        int tipo;

        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(caminho, sizeof(caminho), "%.*s/%s",
                 (int) sizeof(caminho) - 260, dir, e->d_name);
        if (stat(caminho, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            varrer_dir(lib, caminho, nivel + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode) || st.st_size == 0) {
            continue;
        }
        ext = extensao(e->d_name);
        if (casar_extensao(ext, EXT_AUDIO)) {
            tipo = WM_MIDIA_AUDIO;
        } else if (casar_extensao(ext, EXT_VIDEO)) {
            tipo = WM_MIDIA_VIDEO;
        } else {
            continue;
        }
        adicionar(lib, caminho, e->d_name, tipo, &st);
    }
    closedir(d);
}

static void varrer(Library *lib)
{
    for (int i = 0; i < lib->n_raizes; i++) {
        varrer_dir(lib, lib->raizes[i], 0);
    }
}

static int comparar_itens(const void *a, const void *b)
{
    const struct MediaItem *x = a;
    const struct MediaItem *y = b;
    int cmp;

    if (x->tipo != y->tipo) {
        return x->tipo - y->tipo;
    }
    cmp = wm_casecmp(x->artista, y->artista);
    if (cmp) {
        return cmp;
    }
    cmp = wm_casecmp(x->album, y->album);
    if (cmp) {
        return cmp;
    }
    return wm_casecmp(x->titulo, y->titulo);
}

static void *thread_varredura(void *arg)
{
    Library *lib = arg;

    wm_log("library: varrendo %d diretorio(s)", lib->n_raizes);
    varrer(lib);
    qsort(lib->itens, (size_t) lib->n_itens, sizeof(*lib->itens), comparar_itens);

    pthread_mutex_lock(&lib->lock);
    lib->rodando = false;
    lib->pronto = true;
    lib->versao++;
    pthread_mutex_unlock(&lib->lock);

    {
        int audio = 0, video = 0;
        for (int i = 0; i < lib->n_itens; i++) {
            if (lib->itens[i].tipo == WM_MIDIA_AUDIO) {
                audio++;
            } else {
                video++;
            }
        }
        wm_log("library: %d musica(s), %d video(s)", audio, video);
    }
    return NULL;
}

Library *library_create(const char *const *raizes, int n_raizes)
{
    Library *lib = calloc(1, sizeof(*lib));

    if (!lib) {
        return NULL;
    }
    pthread_mutex_init(&lib->lock, NULL);
    for (int i = 0; i < n_raizes && i < 8; i++) {
        snprintf(lib->raizes[lib->n_raizes], WM_PATH_MAX, "%.*s", WM_PATH_MAX - 1, raizes[i]);
        lib->n_raizes++;
    }
    return lib;
}

void library_destroy(Library *lib)
{
    if (!lib) {
        return;
    }
    pthread_mutex_lock(&lib->lock);
    if (lib->rodando) {
        pthread_mutex_unlock(&lib->lock);
        pthread_join(lib->thread, NULL);
    } else {
        pthread_mutex_unlock(&lib->lock);
    }
    pthread_mutex_destroy(&lib->lock);
    free(lib->itens);
    free(lib);
}

void library_scan(Library *lib)
{
    if (!lib) {
        return;
    }
    pthread_mutex_lock(&lib->lock);
    if (lib->rodando) {
        pthread_mutex_unlock(&lib->lock);
        return;
    }
    free(lib->itens);
    lib->itens = NULL;
    lib->n_itens = 0;
    lib->cap_itens = 0;
    lib->pronto = false;
    lib->rodando = true;
    if (pthread_create(&lib->thread, NULL, thread_varredura, lib) != 0) {
        lib->rodando = false;
        wm_log("!!! library: nao criei o thread de varredura");
        pthread_mutex_unlock(&lib->lock);
        return;
    }
    pthread_mutex_unlock(&lib->lock);
}

bool library_scanning(Library *lib)
{
    bool rodando;

    if (!lib) {
        return false;
    }
    pthread_mutex_lock(&lib->lock);
    rodando = lib->rodando;
    pthread_mutex_unlock(&lib->lock);
    return rodando;
}

unsigned library_version(Library *lib)
{
    unsigned v;

    if (!lib) {
        return 0;
    }
    pthread_mutex_lock(&lib->lock);
    v = lib->versao;
    pthread_mutex_unlock(&lib->lock);
    return v;
}

int library_count(Library *lib)
{
    int n;

    if (!lib) {
        return 0;
    }
    pthread_mutex_lock(&lib->lock);
    n = lib->n_itens;
    pthread_mutex_unlock(&lib->lock);
    return n;
}

bool library_item(Library *lib, int indice, struct MediaItem *out)
{
    bool ok = false;

    if (!lib || !out) {
        return false;
    }
    pthread_mutex_lock(&lib->lock);
    if (indice >= 0 && indice < lib->n_itens) {
        *out = lib->itens[indice];
        ok = true;
    }
    pthread_mutex_unlock(&lib->lock);
    return ok;
}

bool library_cover_path(const struct MediaItem *item, char *out, size_t size)
{
    char dir[WM_PATH_MAX];
    char *barra;

    if (!item || !out) {
        return false;
    }
    snprintf(dir, sizeof(dir), "%s", item->caminho);
    barra = strrchr(dir, '/');
    if (!barra) {
        return false;
    }
    *barra = '\0';
    for (int i = 0; COVER_NAMES[i]; i++) {
        for (int j = 0; COVER_EXT[j]; j++) {
            static const char *variantes[] = { "jpg", "JPG", "jpeg", "JPEG",
                                               "png", "PNG", "webp", NULL };
            struct stat st;

            (void) COVER_EXT;
            for (int k = 0; variantes[k]; k++) {
                snprintf(out, size, "%s/%s.%s", dir, COVER_NAMES[i], variantes[k]);
                if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
                    return true;
                }
            }
        }
    }
    out[0] = '\0';
    return false;
}
