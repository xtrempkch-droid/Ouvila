/* tema.c — os cinco temas da UI.
 *
 * As cores vieram dos protótipos HTML em variants/media-player/layout
 * (layout1..5.md). Nada aqui é inventado: cada valor é o que o protótipo usa,
 * convertido de hex para RGB. Trocar de tema é só trocar o ponteiro devolvido
 * por tema_atual(), então a UI inteira muda sem recompilar nada.
 *
 * O layout 4 tem a mesma paleta do 3; o que muda é o realce do item focado
 * (borda quase branca + brilho), sinalizado por `brilho_foco`.
 */

#include "wm.h"

static const Tema TEMAS[WM_TEMA_TOTAL] = {
    /* layout1: "neon" — azul-noite com acento rosa */
    [WM_TEMA_NEON] = {
        .codigo = "neon",
        .msg_nome = MSG_TEMA_NEON,
        .fundo = { 0x0B, 0x0F, 0x19, 0xFF },
        .fundo2 = { 0x07, 0x09, 0x0E, 0xFF },
        .painel = { 0x16, 0x1E, 0x2E, 0xFF },
        .painel2 = { 0x1C, 0x25, 0x38, 0xFF },
        .borda = { 0x2A, 0x37, 0x4E, 0xFF },
        .acento = { 0xF4, 0x3F, 0x5E, 0xFF },
        .acento_esc = { 0xE1, 0x1D, 0x48, 0xFF },
        .texto = { 0xF1, 0xF5, 0xF9, 0xFF },
        .muted = { 0x94, 0xA3, 0xB8, 0xFF },
        .barra = { 0x2A, 0x37, 0x4E, 0xFF },
        .veu = 200,
        .brilho_foco = false,
    },
    /* layout2: OLED vermelho — preto fundo, acento #EF4444 */
    [WM_TEMA_OLED] = {
        .codigo = "oled",
        .msg_nome = MSG_TEMA_OLED,
        .fundo = { 0x02, 0x04, 0x08, 0xFF },
        .fundo2 = { 0x00, 0x00, 0x00, 0xFF },
        .painel = { 0x0D, 0x11, 0x17, 0xFF },
        .painel2 = { 0x16, 0x1B, 0x22, 0xFF },
        .borda = { 0x1F, 0x29, 0x37, 0xFF },
        .acento = { 0xEF, 0x44, 0x44, 0xFF },
        .acento_esc = { 0xDC, 0x26, 0x26, 0xFF },
        .texto = { 0xF3, 0xF4, 0xF6, 0xFF },
        .muted = { 0x6B, 0x72, 0x80, 0xFF },
        .barra = { 0x1F, 0x29, 0x37, 0xFF },
        .veu = 215,
        .brilho_foco = false,
    },
    /* layout3: monocromático — preto puro, acento branco */
    [WM_TEMA_MONO] = {
        .codigo = "mono",
        .msg_nome = MSG_TEMA_MONO,
        .fundo = { 0x00, 0x00, 0x00, 0xFF },
        .fundo2 = { 0x00, 0x00, 0x00, 0xFF },
        .painel = { 0x08, 0x08, 0x08, 0xFF },
        .painel2 = { 0x12, 0x12, 0x12, 0xFF },
        .borda = { 0x17, 0x17, 0x17, 0xFF },
        .acento = { 0xFF, 0xFF, 0xFF, 0xFF },
        .acento_esc = { 0xE5, 0xE5, 0xE5, 0xFF },
        .texto = { 0xFA, 0xFA, 0xFA, 0xFF },
        .muted = { 0x73, 0x73, 0x73, 0xFF },
        .barra = { 0x17, 0x17, 0x17, 0xFF },
        .veu = 220,
        .brilho_foco = false,
    },
    /* layout4: o 3 com foco brilhante (.gamepad-focus) */
    [WM_TEMA_MONO_GLOW] = {
        .codigo = "mono-glow",
        .msg_nome = MSG_TEMA_MONO_GLOW,
        .fundo = { 0x00, 0x00, 0x00, 0xFF },
        .fundo2 = { 0x00, 0x00, 0x00, 0xFF },
        .painel = { 0x08, 0x08, 0x08, 0xFF },
        .painel2 = { 0x18, 0x18, 0x18, 0xF2 },
        .borda = { 0xE5, 0xE5, 0xE5, 0xE6 },
        .acento = { 0xFF, 0xFF, 0xFF, 0xFF },
        .acento_esc = { 0xE5, 0xE5, 0xE5, 0xFF },
        .texto = { 0xFA, 0xFA, 0xFA, 0xFF },
        .muted = { 0x73, 0x73, 0x73, 0xFF },
        .barra = { 0x17, 0x17, 0x17, 0xFF },
        .veu = 220,
        .brilho_foco = true,
    },
    /* layout5: vidro (iOS) — painéis translúcidos sobre preto */
    [WM_TEMA_VIDRO] = {
        .codigo = "vidro",
        .msg_nome = MSG_TEMA_VIDRO,
        .fundo = { 0x00, 0x00, 0x00, 0xFF },
        .fundo2 = { 0x0A, 0x0A, 0x0C, 0xFF },
        .painel = { 0x1C, 0x1C, 0x1E, 0xBF },
        .painel2 = { 0x2C, 0x2C, 0x2E, 0xD9 },
        .borda = { 0xFF, 0xFF, 0xFF, 0x1F },
        .acento = { 0xFF, 0xFF, 0xFF, 0xFF },
        .acento_esc = { 0xD1, 0xD1, 0xD6, 0xFF },
        .texto = { 0xF2, 0xF2, 0xF7, 0xFF },
        .muted = { 0x8E, 0x8E, 0x93, 0xFF },
        .barra = { 0xFF, 0xFF, 0xFF, 0x1F },
        .veu = 170,
        .brilho_foco = false,
    },
};

/* Neon é o padrão: é o protótipo mais "de produto" e o que o usuário viu
 * primeiro. O valor gravado nos ajustes tem prioridade no boot. */
static int g_tema = WM_TEMA_NEON;

int tema_atual_id(void)
{
    return g_tema;
}

void tema_set(int id)
{
    if (id < 0 || id >= WM_TEMA_TOTAL) {
        id = WM_TEMA_NEON;
    }
    g_tema = id;
}

const Tema *tema_atual(void)
{
    return &TEMAS[g_tema];
}

int tema_por_codigo(const char *codigo)
{
    if (!codigo || !codigo[0]) {
        return WM_TEMA_NEON;
    }
    for (int i = 0; i < WM_TEMA_TOTAL; i++) {
        if (wm_casecmp(codigo, TEMAS[i].codigo) == 0) {
            return i;
        }
    }
    return WM_TEMA_NEON;
}

const char *tema_codigo(int id)
{
    if (id < 0 || id >= WM_TEMA_TOTAL) {
        return TEMAS[WM_TEMA_NEON].codigo;
    }
    return TEMAS[id].codigo;
}

int tema_msg_nome(int id)
{
    if (id < 0 || id >= WM_TEMA_TOTAL) {
        return TEMAS[WM_TEMA_NEON].msg_nome;
    }
    return TEMAS[id].msg_nome;
}
