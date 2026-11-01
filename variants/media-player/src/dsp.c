/* dsp.c — equalizador de 10 bandas e limitador.
 *
 * Tudo em software, no caminho do áudio, antes do snd_pcm_writei. Não há
 * plugin do ALSA nem servidor de som: o app é um binário só.
 *
 * Estrutura: por canal, dez filtros "peaking" (receita RBJ) em cascata, um por
 * banda de oitava. O ganho de cada banda vem dos ajustes e é aplicado em dB.
 * Banda com ganho 0 é *pulada*, então com o preset "Plano" o custo é zero.
 *
 * Depois da cascata vem a pré-amplificação (para compensar o ganho que o EQ
 * soma) e um limitador suave cúbico, que impede o estouro de 16 bits sem o
 * "estalo" do corte duro.
 *
 * Modo neural: o RK3566 tem NPU, mas o driver só existe no kernel de fornecedor
 * (/dev/rknpu). No mainline não há esse dispositivo, então o modo fica
 * desativado e o item do menu aparece apagado — foi o que o usuário pediu
 * ("em caso de não disponibilidade o botão não poderá ser ativado").
 */

#include "wm.h"

#include <math.h>
#include <string.h>
#include <unistd.h>

#define EQ_MAX_CANAIS 2
#define EQ_DB_MIN (-12.0f)
#define EQ_DB_MAX 12.0f

/* Bandas de oitava, de 31 Hz a 16 kHz. */
const int wm_eq_freqs[WM_EQ_BANDAS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

/* Presets. "Personalizado" não é uma tabela: é o que o usuário deixou. */
static const float PRESETS[WM_PRESET_PERSONALIZADO][WM_EQ_BANDAS] = {
    /* plano */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* rock: sorriso clássico */
    { 5, 4, 3, 1, -1, -1, 0, 2, 3, 4 },
    /* pop: médio-alto em evidência */
    { -1, 0, 2, 4, 4, 2, 0, -1, -1, -1 },
    /* jazz: corpo nos graves, presença leve */
    { 3, 2, 1, 2, -1, -1, 0, 1, 2, 3 },
    /* clássico: quase plano, um pouco de palco */
    { 3, 2, 1, 0, 0, 0, -1, -1, 1, 2 },
    /* voz: corta o que atrapalha e realça a faixa falada */
    { -4, -3, -1, 1, 3, 4, 4, 3, 1, -2 },
    /* graves */
    { 7, 6, 5, 3, 1, 0, 0, 0, 0, 0 },
    /* eletrônico */
    { 5, 4, 2, 0, -1, 1, 2, 4, 5, 5 },
};

typedef struct {
    /* Coeficientes já normalizados por a0 (forma direta I). */
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    bool ativo;
} Biquad;

static struct {
    bool pronto;
    int rate;
    int canais;
    float ganho_db[WM_EQ_BANDAS];
    float preamp_db;
    int preset;
    Biquad eq[EQ_MAX_CANAIS][WM_EQ_BANDAS];
    float preamp;
    bool neural;
} g;

static bool g_npu_checado;
static bool g_npu_presente;
static char g_npu_nome[64];

/* --------------------------------------------------------------- NPU --- */

static void npu_checar(void)
{
    static const char *const LOCAIS[] = {
        "/dev/rknpu",
        "/sys/class/misc/rknpu",
        "/dev/dri/renderD128",
        NULL
    };

    if (g_npu_checado) {
        return;
    }
    g_npu_checado = true;
    g_npu_presente = false;
    g_npu_nome[0] = '\0';

    /* O renderD128 sozinho não é NPU (é a GPU Panfrost/Mali), então ele é
     * checado separado: só conta se o driver for rockchip,rknpu. */
    for (int i = 0; LOCAIS[i]; i++) {
        if (access(LOCAIS[i], F_OK) != 0) {
            continue;
        }
        if (strcmp(LOCAIS[i], "/dev/dri/renderD128") == 0) {
            continue;
        }
        g_npu_presente = true;
        snprintf(g_npu_nome, sizeof(g_npu_nome), "%s", LOCAIS[i]);
        return;
    }

    /* Segundo caminho: o kernel vendor publica o NPU como platform device. */
    if (access("/sys/bus/platform/drivers/rknpu", F_OK) == 0) {
        g_npu_presente = true;
        snprintf(g_npu_nome, sizeof(g_npu_nome), "rknpu (platform)");
    }
}

bool wm_npu_presente(void)
{
    npu_checar();
    return g_npu_presente;
}

const char *wm_npu_nome(void)
{
    npu_checar();
    return g_npu_nome[0] ? g_npu_nome : T(MSG_SOM_NPU_AUSENTE);
}

/* ---------------------------------------------------------- filtros --- */

/* Coeficientes de um peaking EQ (Robert Bristow-Johnson, "audio EQ cookbook").
 * Q = 1.2 dá bandas de ~1 oitava com pouca sobreposição nos extremos, que é o
 * padrão dos equalizadores gráficos de 10 bandas. */
static void biquad_peaking(Biquad *q, int freq, float db, int rate)
{
    float a = powf(10.0f, db / 40.0f);
    float w0 = 2.0f * (float) M_PI * (float) freq / (float) rate;
    float cos_w0 = cosf(w0);
    float alpha = sinf(w0) / (2.0f * 1.2f);
    float a0;

    if (freq >= rate / 2) {
        /* Acima de Nyquist o filtro não tem sentido: vira passagem direta. */
        q->b0 = 1.0f;
        q->b1 = q->b2 = q->a1 = q->a2 = 0.0f;
        q->ativo = false;
        return;
    }

    q->b0 = 1.0f + alpha * a;
    q->b1 = -2.0f * cos_w0;
    q->b2 = 1.0f - alpha * a;
    a0 = 1.0f + alpha / a;
    q->a1 = -2.0f * cos_w0 / a0;
    q->a2 = (1.0f - alpha / a) / a0;
    q->b0 = q->b0 / a0;
    q->b1 = q->b1 / a0;
    q->b2 = q->b2 / a0;
    q->ativo = fabsf(db) > 0.05f;
}

static void recalcular(void)
{
    for (int c = 0; c < EQ_MAX_CANAIS; c++) {
        for (int b = 0; b < WM_EQ_BANDAS; b++) {
            biquad_peaking(&g.eq[c][b], wm_eq_freqs[b], g.ganho_db[b], g.rate);
        }
    }
    g.preamp = powf(10.0f, g.preamp_db / 20.0f);
    g.pronto = true;
}

static void limpar_estado(void)
{
    for (int c = 0; c < EQ_MAX_CANAIS; c++) {
        for (int b = 0; b < WM_EQ_BANDAS; b++) {
            g.eq[c][b].x1 = g.eq[c][b].x2 = 0.0f;
            g.eq[c][b].y1 = g.eq[c][b].y2 = 0.0f;
        }
    }
}

/* ------------------------------------------------------------- API --- */

void dsp_init(int rate, int canais)
{
    if (rate <= 0) {
        rate = 44100;
    }
    if (canais <= 0 || canais > EQ_MAX_CANAIS) {
        canais = 2;
    }
    if (g.pronto && g.rate == rate && g.canais == canais) {
        return;
    }
    g.rate = rate;
    g.canais = canais;
    g.preamp_db = 0.0f;
    for (int b = 0; b < WM_EQ_BANDAS; b++) {
        g.ganho_db[b] = 0.0f;
    }
    g.preset = WM_PRESET_PLANO;
    g.neural = false;
    limpar_estado();
    recalcular();
}

void dsp_reset(void)
{
    for (int b = 0; b < WM_EQ_BANDAS; b++) {
        g.ganho_db[b] = 0.0f;
    }
    g.preamp_db = 0.0f;
    g.preset = WM_PRESET_PLANO;
    limpar_estado();
    recalcular();
}

void dsp_set_banda(int banda, float db)
{
    if (banda < 0 || banda >= WM_EQ_BANDAS) {
        return;
    }
    if (db < EQ_DB_MIN) {
        db = EQ_DB_MIN;
    }
    if (db > EQ_DB_MAX) {
        db = EQ_DB_MAX;
    }
    g.ganho_db[banda] = db;
    g.preset = WM_PRESET_PERSONALIZADO;
    recalcular();
}

float dsp_banda(int banda)
{
    if (banda < 0 || banda >= WM_EQ_BANDAS) {
        return 0.0f;
    }
    return g.ganho_db[banda];
}

void dsp_set_preamp(float db)
{
    if (db < EQ_DB_MIN) {
        db = EQ_DB_MIN;
    }
    if (db > EQ_DB_MAX) {
        db = EQ_DB_MAX;
    }
    g.preamp_db = db;
    recalcular();
}

float dsp_preamp(void)
{
    return g.preamp_db;
}

void dsp_aplicar_preset(int preset)
{
    if (preset < 0 || preset >= WM_PRESET_TOTAL) {
        preset = WM_PRESET_PLANO;
    }
    if (preset == WM_PRESET_PERSONALIZADO) {
        /* Não mexe nos ganhos: é o estado que o usuário montou. */
        g.preset = preset;
        return;
    }
    for (int b = 0; b < WM_EQ_BANDAS; b++) {
        g.ganho_db[b] = PRESETS[preset][b];
    }
    g.preset = preset;
    recalcular();
}

int dsp_preset(void)
{
    return g.preset;
}

int dsp_msg_preset(int preset)
{
    static const int MSGS[WM_PRESET_TOTAL] = {
        MSG_PRESET_PLANO, MSG_PRESET_ROCK, MSG_PRESET_POP, MSG_PRESET_JAZZ,
        MSG_PRESET_CLASSICO, MSG_PRESET_VOZ, MSG_PRESET_GRAVES,
        MSG_PRESET_ELETRONICO, MSG_PRESET_PERSONALIZADO
    };

    if (preset < 0 || preset >= WM_PRESET_TOTAL) {
        return MSG_PRESET_PLANO;
    }
    return MSGS[preset];
}

bool dsp_neural_disponivel(void)
{
    return wm_npu_presente();
}

bool dsp_neural(void)
{
    return g.neural;
}

bool dsp_set_neural(bool ligado)
{
    if (ligado && !dsp_neural_disponivel()) {
        g.neural = false;
        return false;
    }
    g.neural = ligado;
    return true;
}

/* ------------------------------------------------------- processamento --- */

/* Limitador de teto: abaixo de 0,5 passa intacto (a imensa maioria das
 * amostras) e acima disso comprime numa curva racional que nunca passa de 1.
 * Monotônica, então não distorce a forma de onda; e sem `exp`/`tanh`, que
 * custariam caro em ~90 mil amostras por segundo. */
static inline float limitar(float x)
{
    float a = fabsf(x);
    float s = x < 0.0f ? -1.0f : 1.0f;

    if (a <= 0.5f) {
        return x;
    }
    a = 0.5f + 0.5f * ((a - 0.5f) / (a + 0.5f));
    if (a > 1.0f) {
        a = 1.0f;
    }
    return s * a;
}

static inline int16_t para_i16(float x)
{
    x *= 32768.0f;
    if (x > 32767.0f) {
        x = 32767.0f;
    }
    if (x < -32768.0f) {
        x = -32768.0f;
    }
    return (int16_t) lrintf(x);
}

void dsp_processar(int16_t *dados, int frames)
{
    bool tem_eq = false;
    bool precisa_limitar;

    if (!g.pronto || !dados || frames <= 0) {
        return;
    }
    for (int b = 0; b < WM_EQ_BANDAS; b++) {
        if (g.eq[0][b].ativo) {
            tem_eq = true;
            break;
        }
    }
    if (!tem_eq && g.preamp_db == 0.0f) {
        return;   /* nada a fazer: caminho rápido com o EQ plano */
    }

    /* Só há risco de estouro quando alguma banda ou o preamp SOMA ganho. */
    precisa_limitar = g.preamp_db > 0.0f;
    for (int b = 0; b < WM_EQ_BANDAS && !precisa_limitar; b++) {
        precisa_limitar = g.ganho_db[b] > 0.0f;
    }

    for (int i = 0; i < frames; i++) {
        for (int c = 0; c < g.canais; c++) {
            int idx = i * g.canais + c;
            float x = (float) dados[idx] / 32768.0f;

            for (int b = 0; b < WM_EQ_BANDAS; b++) {
                Biquad *q = &g.eq[c][b];
                float y;

                if (!q->ativo) {
                    continue;
                }
                y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2
                    - q->a1 * q->y1 - q->a2 * q->y2;
                q->x2 = q->x1;
                q->x1 = x;
                q->y2 = q->y1;
                q->y1 = y;
                /* Estado instável ou subnormal: zera para o filtro não "explodir"
                 * (ruído no áudio) nem cair em aritmética lenta. */
                if (!(y > -8.0f && y < 8.0f)) {
                    q->x1 = q->x2 = q->y1 = q->y2 = 0.0f;
                    y = 0.0f;
                }
                x = y;
            }

            x *= g.preamp;
            if (precisa_limitar) {
                x = limitar(x);
            }
            dados[idx] = para_i16(x);
        }
    }
}

/* Presets para o menu: quantidade de itens (usada pelo ui_menu). */
int dsp_preset_total(void)
{
    return WM_PRESET_TOTAL;
}
