#!/usr/bin/env bash
# Monta um sysroot arm64 (bookworm) com os cabeçalhos e bibliotecas que o app de
# mídia usa, para cross-compilar no runner.
#
# Por que não instalar os -dev no runner: os pacotes arm64 são de outra
# arquitetura e o apt se recusa. Por que não usar os do runner (x86_64): o
# código tem de compilar contra o FFmpeg **5.1** que a imagem terá, e não contra
# a versão da máquina de build.
#
# Detalhe que quebra o link e não aparece no `-fsyntax-only`: no Debian o
# arquivo `.so` é um symlink que aponta para `libXXX.so.N`, e o **alvo só existe
# no pacote de runtime**. Baixar apenas os `-dev` resolve os cabeçalhos e falha
# no `-l`. Por isso a lista tem os dois e o script confere no fim que cada `-l`
# usado pelo app tem biblioteca de verdade (não só o symlink).
#
# Uso:
#   build/media-player-sysroot.sh DESTINO
#   build/media-player-sysroot.sh DESTINO --keep-debs   (útil para depurar)
set -euo pipefail

DESTINO=${1:-}
if [ -z "$DESTINO" ]; then
    echo "uso: $0 <diretorio-do-sysroot>" >&2
    exit 2
fi
KEEP_DEBS=0
if [ "${2:-}" = "--keep-debs" ]; then
    KEEP_DEBS=1
fi

SUITE=${WM_SUITE:-bookworm}
MIRROR=${WM_MIRROR:-http://deb.debian.org/debian}
ARCH=${WM_ARCH:-arm64}
TRIPLE=aarch64-linux-gnu

# Cabeçalhos (-dev) e bibliotecas de link (runtime). O runtime é obrigatório:
# ver o comentário do topo.
DEV=(
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
    libdrm-dev libasound2-dev libfreetype-dev
)
# Dependências de cabeçalho que os -dev acima incluem e que não vêm no runner.
EXTRA_DEV=(
    libpng-dev zlib1g-dev libbrotli-dev libpciaccess-dev libexpat1-dev
)
RUNTIME=(
    libavformat59 libavcodec59 libavutil57 libswscale6 libswresample4
    libdrm2 libasound2 libfreetype6 libpng16-16 zlib1g
)

mkdir -p "$DESTINO"
CACHE="$DESTINO/.debs"
mkdir -p "$CACHE"

echo "--> baixando o índice $SUITE/$ARCH"
curl -fsSL "$MIRROR/dists/$SUITE/main/binary-$ARCH/Packages.gz" -o "$CACHE/Packages.gz"
gzip -dc "$CACHE/Packages.gz" > "$CACHE/Packages.txt"

baixar() {
    local p=$1 url
    # -A 40: o bloco Depends/Description de alguns pacotes é longo; sem isso o
    # Filename do próximo pacote entra e o download vai para o arquivo errado.
    url=$(grep -A 40 "^Package: $p$" "$CACHE/Packages.txt" \
            | sed -n 's/^Filename: //p' | head -1)
    if [ -z "$url" ]; then
        echo "!!! $p nao existe para $ARCH no $SUITE" >&2
        echo "!!! (nao use packages.debian.org: ele responde 200 com pagina de erro)" >&2
        return 1
    fi
    local deb="$CACHE/$(basename "$url")"
    if [ ! -f "$deb" ]; then
        curl -fsSL -o "$deb" "$MIRROR/$url"
    fi
    dpkg-deb -x "$deb" "$DESTINO"
    echo "    $p"
}

echo "--> cabeçalhos (-dev)"
for p in "${DEV[@]}" "${EXTRA_DEV[@]}"; do
    baixar "$p"
done

echo "--> bibliotecas para o link (runtime)"
for p in "${RUNTIME[@]}"; do
    baixar "$p"
done

echo "--> conferindo cabeçalhos obrigatórios"
for h in \
    usr/include/xf86drm.h usr/include/xf86drmMode.h usr/include/alsa/asoundlib.h \
    usr/include/freetype2/ft2build.h usr/include/$TRIPLE/libavformat/avformat.h \
    usr/include/$TRIPLE/libavcodec/avcodec.h usr/include/$TRIPLE/libswscale/swscale.h \
    usr/include/$TRIPLE/libswresample/swresample.h
do
    if [ ! -e "$DESTINO/$h" ]; then
        echo "!!! cabeçalho ausente no sysroot: $h" >&2
        exit 1
    fi
done

# Cada -l do build precisa de um .so (symlink do -dev) cujo ALVO exista.
# É exatamente o furo que aparece como "cannot find -lavformat" no CI.
echo "--> conferindo bibliotecas de link"
FALTOU=0
for lib in avformat avcodec avutil swscale swresample drm asound freetype; do
    alvo=$(find "$DESTINO/usr/lib" -name "lib$lib.so" -type l 2>/dev/null | head -1)
    if [ -z "$alvo" ]; then
        echo "!!! lib$lib.so ausente (o pacote -dev nao foi extraido?)" >&2
        FALTOU=$((FALTOU+1))
        continue
    fi
    real=$(dirname "$alvo")/$(readlink "$alvo")
    if [ ! -e "$real" ]; then
        echo "!!! $alvo aponta para $(basename "$real"), que nao existe" >&2
        echo "!!! falta o pacote de runtime correspondente na lista RUNTIME" >&2
        FALTOU=$((FALTOU+1))
    fi
done
if [ "$FALTOU" -gt 0 ]; then
    echo "!!! sysroot incompleto ($FALTOU biblioteca(s))" >&2
    exit 1
fi

if [ "$KEEP_DEBS" -eq 0 ]; then
    rm -rf "$CACHE"
fi

echo "--> sysroot pronto em $DESTINO ($(du -sh "$DESTINO" | cut -f1))"
