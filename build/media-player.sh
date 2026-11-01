#!/usr/bin/env bash
# Compila o app de mídia da variante `media-player` para arm64.
#
# O app é um binário único (DRM/KMS + FFmpeg + ALSA + FreeType), então o build
# não instala toolkits: ele só precisa de cabeçalhos da arquitetura de destino e
# de um compilador cruzado.
#
# Uso:
#   build/media-player.sh                      # cross arm64 (CI)
#   build/media-player.sh --native             # compila para a máquina local
#   build/media-player.sh --check              # só valida (sintaxe) contra o
#                                              # sysroot arm64 do bookworm
#
# Variáveis:
#   WM_SYSROOT      diretório com usr/include e usr/lib da arquitetura (obrigatório no cross)
#   WM_CROSS        prefixo do cross-compiler (padrão aarch64-linux-gnu-)
#   WM_CC           compilador (padrão ${WM_CROSS}gcc, ou gcc no --native)
#   WM_OUT          arquivo de saída (padrão build/walkmam-media)
#
# Por que cross-compilar em vez de compilar dentro do rootfs: instalar
# gcc + libavformat-dev dentro do rootfs Debian arm64 adicionaria ~500 MB à
# imagem e exigiria qemu para cada compilação. O cross-compiler no runner é o
# mesmo usado para o kernel, e os cabeçalhos vêm dos pacotes arm64 do bookworm
# (mesma versão de biblioteca que a imagem terá: FFmpeg 5.1).
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SRC_DIR="$ROOT_DIR/variants/media-player/src"
OUT="${WM_OUT:-$ROOT_DIR/build/walkmam-media}"
MODO=cross

for arg in "$@"; do
    case "$arg" in
        --native) MODO=native ;;
        --check)  MODO=check ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "!!! opcao desconhecida: $arg" >&2; exit 2 ;;
    esac
done

if [ ! -d "$SRC_DIR" ]; then
    echo "!!! fontes nao encontradas em $SRC_DIR" >&2
    exit 1
fi

case "$MODO" in
    native)
        CC=${WM_CC:-gcc}
        SYSROOT_INC=""
        LINK=(-lavformat -lavcodec -lavutil -lswscale -lswresample -lasound -lfreetype -ldrm -lpthread -lm)
        if [ -n "${WM_SYSROOT:-}" ]; then
            # Sysroot local (sem root para instalar os -dev): os headers do
            # FFmpeg ficam em usr/include/<triple>/, como no Debian.
            SYSROOT_INC="-I$WM_SYSROOT/usr/include -I$WM_SYSROOT/usr/include/libdrm -I$WM_SYSROOT/usr/include/freetype2"
            for triple in x86_64-linux-gnu aarch64-linux-gnu; do
                [ -d "$WM_SYSROOT/usr/include/$triple" ] && \
                    SYSROOT_INC="$SYSROOT_INC -I$WM_SYSROOT/usr/include/$triple"
                [ -d "$WM_SYSROOT/usr/lib/$triple" ] && \
                    LINK+=(-L"$WM_SYSROOT/usr/lib/$triple")
            done
        fi
        ;;
    cross|check)
        CROSS=${WM_CROSS:-aarch64-linux-gnu-}
        CC=${WM_CC:-${CROSS}gcc}
        if [ -z "${WM_SYSROOT:-}" ] || [ ! -d "$WM_SYSROOT" ]; then
            echo "!!! WM_SYSROOT nao definido (precisa conter usr/include do arm64)" >&2
            exit 1
        fi
        SYSROOT_INC="-I$WM_SYSROOT/usr/include -I$WM_SYSROOT/usr/include/aarch64-linux-gnu -I$WM_SYSROOT/usr/include/libdrm -I$WM_SYSROOT/usr/include/freetype2"
        LINK=(-L"$WM_SYSROOT/usr/lib/aarch64-linux-gnu" -L"$WM_SYSROOT/usr/lib"
              -lavformat -lavcodec -lavutil -lswscale -lswresample -lasound -lfreetype -ldrm -lpthread -lm)
        ;;
esac

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "!!! compilador nao encontrado: $CC" >&2
    exit 1
fi

# Os mesmos avisos do kernel: -Werror não é usado porque um aviso de nova versão
# de compilador não deve quebrar a imagem; mas os avisos aparecem no log.
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter)

FONTES=(audio.c decoder.c drm.c font.c gfx.c input.c library.c main.c radio.c ui.c util.c)

echo "--> compilando walkmam-media ($MODO) com $CC"
for f in "${FONTES[@]}"; do
    [ -f "$SRC_DIR/$f" ] || { echo "!!! fonte ausente: $SRC_DIR/$f" >&2; exit 1; }
done

if [ "$MODO" = check ]; then
    rc=0
    for f in "${FONTES[@]}"; do
        if ! "$CC" "${CFLAGS[@]}" -fsyntax-only $SYSROOT_INC "$SRC_DIR/$f"; then
            echo "!!! erro de compilacao em $f" >&2
            rc=1
        fi
    done
    [ "$rc" -eq 0 ] && echo "--> ok: todas as fontes compilam contra o sysroot"
    exit "$rc"
fi

mkdir -p "$(dirname -- "$OUT")"
"$CC" "${CFLAGS[@]}" $SYSROOT_INC \
    -DWM_VERSION='"1.0.0"' \
    -o "$OUT" "${FONTES[@]/#/$SRC_DIR/}" \
    "${LINK[@]}"

if [ "$MODO" = cross ]; then
    # Garante que o binário é da arquitetura certa: instalar um x86_64 no
    # rootfs arm64 só apareceria no aparelho, como "exec format error".
    if command -v file >/dev/null 2>&1; then
        if ! file "$OUT" | grep -q 'ARM aarch64'; then
            echo "!!! o binario nao e aarch64:" >&2
            file "$OUT" >&2
            exit 1
        fi
        echo "--> arquitetura conferida: $(file -b "$OUT")"
    fi
fi

echo "--> binario: $OUT ($(stat -c%s "$OUT") bytes)"
