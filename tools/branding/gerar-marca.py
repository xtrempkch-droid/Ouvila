#!/usr/bin/env python3
"""Gera a arte de marca do Ouvila a partir de docs/logo.jpg.

O logo tem TRES grupos (icone + rotulo) lado a lado. A ordem e a informada pelo
autor do projeto (nao adivinhada):

    esquerda = musica (media-player)
    centro   = jogos  (RetroArch)
    direita  = video  (Kodi)

Saidas:
  variants/<v>/overlay/usr/share/ouvila/branding/icone.png   icone quadrado do app
  common/overlay/usr/share/plymouth/themes/ouvila/splash-paisagem.png
  common/overlay/usr/share/plymouth/themes/ouvila/splash-retrato.png

Por que DUAS telas de boot: o painel da X55 e 720x1280 (retrato) e o do X35 e
640x480 (paisagem). O Plymouth desenha ANTES de qualquer rotacao (a rotacao e do
Weston), entao a arte precisa existir nos dois sentidos; o tema escolhe pelo
tamanho da tela em tempo de execucao.

Uso: python3 tools/branding/gerar-marca.py [--logo docs/logo.jpg]
Requer Pillow.
"""
import argparse
import os
import sys

try:
    from PIL import Image, ImageFilter
except ImportError:
    sys.exit("precisa de Pillow: pip install pillow")

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Caixas medidas por projecao de luminosidade no logo 1408x768 (colunas com
# conteudo e, dentro de cada uma, a faixa de linhas do icone). Se o logo mudar,
# rode com --inspecionar para ver as caixas novas.
GRUPOS = {
    "musica": (144, 406, 229, 402),
    "jogos":  (556, 852, 205, 408),
    "video":  (988, 1275, 220, 404),
}

VARIANTES = {
    "musica": "media-player",
    "jogos":  "retroarch",
    "video":  "kodi",
}

LADO_ICONE = 512          # icone quadrado exportado
SPLASH_PAISAGEM = (1280, 720)
SPLASH_RETRATO = (720, 1280)


def caixa_do_conteudo(im, limite=60):
    """Recorta a area util (pixels mais claros que o fundo) de um bloco."""
    cinza = im.convert("L")
    px = cinza.load()
    w, h = cinza.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if px[x, y] > limite:
                xs.append(x)
                ys.append(y)
    if not xs:
        return (0, 0, w, h)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1)


def quadrado(im, fundo):
    """Centraliza o recorte num quadrado com o mesmo fundo (sem distorcer)."""
    w, h = im.size
    lado = max(w, h)
    tela = Image.new("RGB", (lado, lado), fundo)
    tela.paste(im, ((lado - w) // 2, (lado - h) // 2))
    return tela


def cor_de_fundo(im):
    """Cor do canto superior esquerdo (o fundo do logo)."""
    return im.convert("RGB").getpixel((2, 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logo", default=os.path.join(RAIZ, "docs", "logo.jpg"))
    ap.add_argument("--inspecionar", action="store_true",
                    help="so mostra as caixas medidas, nao escreve arquivos")
    args = ap.parse_args()

    if not os.path.exists(args.logo):
        sys.exit(f"logo nao encontrado: {args.logo}")

    logo = Image.open(args.logo).convert("RGB")
    fundo = cor_de_fundo(logo)
    print(f"logo: {logo.size[0]}x{logo.size[1]}, fundo {fundo}")

    for nome, (x0, x1, y0, y1) in GRUPOS.items():
        bloco = logo.crop((x0, y0, x1 + 1, y1 + 1))
        cx0, cy0, cx1, cy1 = caixa_do_conteudo(bloco)
        util = bloco.crop((cx0, cy0, cx1, cy1))
        print(f"  {nome:7s} caixa medida {x0},{y0}..{x1},{y1} -> util {util.size[0]}x{util.size[1]}")

        if args.inspecionar:
            continue

        # 1) icone quadrado da variante
        icone = quadrado(util, fundo).resize((LADO_ICONE, LADO_ICONE), Image.LANCZOS)
        variante = VARIANTES[nome]
        destino = os.path.join(RAIZ, "variants", variante, "overlay", "usr", "share",
                               "ouvila", "branding", "icone.png")
        os.makedirs(os.path.dirname(destino), exist_ok=True)
        icone.save(destino, optimize=True)
        print(f"          icone -> {os.path.relpath(destino, RAIZ)} ({os.path.getsize(destino)//1024} KB)")

    if args.inspecionar:
        return

    # 2) telas de boot: a arte inteira, centralizada, sobre o mesmo fundo
    tema = os.path.join(RAIZ, "common", "overlay", "usr", "share", "plymouth",
                        "themes", "ouvila")
    os.makedirs(tema, exist_ok=True)
    for nome, alvo in (("splash-paisagem.png", SPLASH_PAISAGEM),
                       ("splash-retrato.png", SPLASH_RETRATO)):
        lw, lh = alvo
        # margem de 4% para a arte nao encostar na borda
        disp_w, disp_h = int(lw * 0.92), int(lh * 0.92)
        escala = min(disp_w / logo.width, disp_h / logo.height)
        nova = (max(1, int(logo.width * escala)), max(1, int(logo.height * escala)))
        arte = logo.resize(nova, Image.LANCZOS)
        tela = Image.new("RGB", alvo, fundo)
        tela.paste(arte, ((lw - nova[0]) // 2, (lh - nova[1]) // 2))
        caminho = os.path.join(tema, nome)
        tela.save(caminho, optimize=True)
        print(f"          {nome} -> {os.path.relpath(caminho, RAIZ)} "
              f"({os.path.getsize(caminho)//1024} KB)")


if __name__ == "__main__":
    main()
