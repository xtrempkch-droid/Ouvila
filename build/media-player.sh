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
#   WM_OUT          arquivo de saída (padrão build/ouvila-media)
#
# Por que cross-compilar em vez de compilar dentro do rootfs: instalar
# gcc + libavformat-dev dentro do rootfs Debian arm64 adicionaria ~500 MB à
# imagem e exigiria qemu para cada compilação. O cross-compiler no runner é o
# mesmo usado para o kernel, e os cabeçalhos vêm dos pacotes arm64 do bookworm
# (mesma versão de biblioteca que a imagem terá: FFmpeg 5.1).
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SRC_DIR="$ROOT_DIR/variants/media-player/src"
OUT="${WM_OUT:-$ROOT_DIR/build/ouvila-media}"
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
        ;;
    cross|check)
        CROSS=${WM_CROSS:-aarch64-linux-gnu-}
        CC=${WM_CC:-${CROSS}gcc}
        ;;
esac

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "!!! compilador nao encontrado: $CC" >&2
    exit 1
fi

# Descobrir os caminhos de cabeçalho é o ponto mais frágil do build, porque
# cada biblioteca escolhe o seu: libdrm instala `drm.h` em /usr/include/libdrm
# (o `xf86drm.h` fica em /usr/include e faz `#include <drm.h>`), FreeType em
# /usr/include/freetype2 e o FFmpeg em /usr/include/<triple>/libavformat.
# Sem esses -I o erro é "fatal error: drm.h: No such file or directory" — foi
# exatamente a falha do CI na primeira tentativa.
HOST_TRIPLE=$("$CC" -dumpmachine 2>/dev/null || echo x86_64-linux-gnu)
case "$MODO" in
    native)
        # WM_BASE_INC permite apontar para uma árvore com o layout de /usr do
        # sistema (usado nos testes: simula a máquina do CI sem instalar os -dev).
        if [ -n "${WM_SYSROOT:-}" ]; then
            BASE_INC="$WM_SYSROOT/usr/include"
        else
            BASE_INC=${WM_BASE_INC:-/usr/include}
        fi
        TRIPLE_FOR_INC="$HOST_TRIPLE"
        ;;
    *)
        TRIPLE_FOR_INC=aarch64-linux-gnu
        BASE_INC="$WM_SYSROOT/usr/include"
        ;;
esac

SYSROOT_INC=""
# Sempre devolve 0: com `set -e`, um `[ -d ... ]` falso no fim da função mata o
# script em silêncio (sem mensagem nenhuma) — a mesma armadilha do
# `EXIT_ACTION=$(...)` documentada no AGENTS.md.
add_inc() {
    if [ -d "$1" ]; then
        SYSROOT_INC="${SYSROOT_INC:+$SYSROOT_INC }-I$1"
    fi
    return 0
}
add_inc "$BASE_INC"
add_inc "$BASE_INC/libdrm"
add_inc "$BASE_INC/freetype2"
# HarfBuzz (shaping dos alfabetos árabe/índico/chinês) fica em harfbuzz/.
add_inc "$BASE_INC/harfbuzz"
add_inc "$BASE_INC/$TRIPLE_FOR_INC"

# pkg-config é a fonte oficial dos caminhos quando os -dev estão instalados
# (caso do runner) — mas SÓ no modo nativo: em cross ele descreve o HOST, e os
# `-I/usr/include/<triple-do-host>` que ele devolve vencem os cabeçalhos
# internos do alvo (glibc/asm), quebrando qualquer include que puxe <sys/*>.
# Foi exatamente o que falhou no CI: só passaram os cabeçalhos que o host também
# tem (drm.h, ft2build.h) e falharam os que só existem no sysroot.
if [ "$MODO" = "native" ] && command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists libavformat libavcodec libavutil libswscale \
            libswresample alsa freetype2 libdrm harfbuzz 2>/dev/null; then
        for extra in $(pkg-config --cflags libavformat libavcodec libavutil \
                            libswscale libswresample alsa freetype2 libdrm \
                            harfbuzz); do
            case " $SYSROOT_INC " in
                *" $extra "*) ;;
                *) SYSROOT_INC="${SYSROOT_INC:+$SYSROOT_INC }$extra" ;;
            esac
        done
    fi
fi

LINK=(-lavformat -lavcodec -lavutil -lswscale -lswresample -lasound -lfreetype -lharfbuzz -ldrm -lpthread -lm)
if [ "$MODO" = "native" ]; then
    if [ -n "${WM_SYSROOT:-}" ]; then
        # Só o triple do HOST: um sysroot arm64 aqui faria o ld pegar
        # bibliotecas de outra arquitetura e falhar no meio do link (ou pior,
        # com "pulando ... incompatível", parecendo falta de pacote).
        if [ ! -d "$WM_SYSROOT/usr/lib/$HOST_TRIPLE" ]; then
            echo "!!! o sysroot nao tem usr/lib/$HOST_TRIPLE" >&2
            echo "!!! (para arm64 use o modo cross, nao --native)" >&2
            exit 1
        fi
        LINK=(-L"$WM_SYSROOT/usr/lib/$HOST_TRIPLE" "${LINK[@]}")
    else
        # Sem sysroot: as bibliotecas vêm do sistema (ou de BASE_INC/../lib,
        # quando WM_BASE_INC aponta para uma árvore de teste).
        BASE_ROOT=$(cd "$BASE_INC/.." 2>/dev/null && pwd)
        if [ -n "$BASE_ROOT" ] && [ -d "$BASE_ROOT/lib/$HOST_TRIPLE" ]; then
            LINK=(-L"$BASE_ROOT/lib/$HOST_TRIPLE" "${LINK[@]}")
        fi
    fi
elif [ -n "${WM_SYSROOT:-}" ]; then
    # -L só acha a biblioteca que a gente pede (-lasound). As dependências
    # internas delas (DT_NEEDED: libva, libssh, libpng, libspeex...) só são
    # procuradas nos caminhos do -rpath-link. Sem isso o link para em
    # "referência não definida para vaCreateSurfaces@VA_API_0.33.0".
    # ATENÇÃO ao /lib: usrmerge só vale no sistema instalado. Ao extrair os
    # .deb com dpkg-deb -x, `dpkg -L zlib1g` diz /usr/lib/… mas o arquivo real é
    # gravado em <sysroot>/lib/aarch64-linux-gnu — sem esse caminho, o link falha
    # em "referência não definida para gztell" (zlib) e getfilecon (libselinux).
    LINK=(-L"$WM_SYSROOT/usr/lib/aarch64-linux-gnu" -L"$WM_SYSROOT/usr/lib"
          -L"$WM_SYSROOT/lib/aarch64-linux-gnu" -L"$WM_SYSROOT/lib"
          -Wl,-rpath-link,"$WM_SYSROOT/usr/lib/aarch64-linux-gnu"
          -Wl,-rpath-link,"$WM_SYSROOT/usr/lib"
          -Wl,-rpath-link,"$WM_SYSROOT/lib/aarch64-linux-gnu"
          -Wl,-rpath-link,"$WM_SYSROOT/lib"
          "${LINK[@]}")
fi

# Flags extra de link (usado nos testes e para casos específicos de toolchain).
if [ -n "${WM_LDFLAGS:-}" ]; then
    # shellcheck disable=SC2206  # divisão por espaço é intencional aqui
    LINK+=(${WM_LDFLAGS})
fi

# Os mesmos avisos do kernel: -Werror não é usado porque um aviso de nova versão
# de compilador não deve quebrar a imagem; mas os avisos aparecem no log.
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter)

FONTES=(audio.c decoder.c dsp.c drm.c font.c gfx.c i18n.c input.c library.c main.c
        radio.c tema.c ui.c util.c)

# Cada cabeçalho obrigatório é testado isoladamente: se faltar, o erro diz qual
# pacote instalar em vez de "fatal error" no meio de um arquivo qualquer.
FALTA_H=0
ERRO_H=/tmp/wm-hcheck.$$.log
for h in drm.h xf86drmMode.h ft2build.h harfbuzz/hb-ft.h alsa/asoundlib.h \
         libavformat/avformat.h libavcodec/avcodec.h \
         libswscale/swscale.h libswresample/swresample.h; do
    if ! printf '#include <%s>\n' "$h" | "$CC" $SYSROOT_INC -x c -fsyntax-only - >"$ERRO_H" 2>&1; then
        if grep -q "No such file or directory" "$ERRO_H"; then
            echo "!!! cabecalho nao encontrado: <$h>" >&2
        else
            # Achou o cabeçalho mas não compila: quase sempre é mistura de
            # cabeçalhos de arquiteturas diferentes. A mensagem do compilador é
            # a única coisa que permite ver isso no log do CI.
            echo "!!! <$h> existe mas nao compila:" >&2
            head -3 "$ERRO_H" | sed 's/^/      /' >&2
        fi
        FALTA_H=$((FALTA_H+1))
    fi
done
rm -f "$ERRO_H"
if [ "$FALTA_H" -gt 0 ]; then
    echo "!!! $FALTA_H cabecalho(s) com problema. Pacotes: libdrm-dev libfreetype-dev" >&2
    echo "!!! libasound2-dev libavformat-dev libavcodec-dev libavutil-dev" >&2
    echo "!!! libswscale-dev libswresample-dev libharfbuzz-dev" >&2
    echo "!!! flags usadas: $SYSROOT_INC" >&2
    exit 1
fi

echo "--> compilando ouvila-media ($MODO) com $CC"
echo "--> includes: $SYSROOT_INC"
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
    if [ "$rc" -eq 0 ]; then
        echo "--> ok: todas as fontes compilam contra o sysroot"
    fi
    exit "$rc"
fi

mkdir -p "$(dirname -- "$OUT")"
"$CC" "${CFLAGS[@]}" $SYSROOT_INC \
    -DWM_VERSION='"1.1.0"' \
    -o "$OUT" "${FONTES[@]/#/$SRC_DIR/}" \
    "${LINK[@]}"

# A imagem roda bookworm (glibc 2.36). Compilar com os cabeçalhos do runner
# (glibc mais novo) pode gerar símbolos que só existem lá — foi o que aconteceu:
# com _GNU_SOURCE, `sscanf`/`strtol`/`fscanf` viram `__isoc23_*`, que exigem
# GLIBC_2.38, e o binário morria no aparelho com "version GLIBC_2.38 not found".
# Esta checagem pega isso em segundos, sem precisar do qemu.
if [ "$MODO" = cross ] && command -v objdump >/dev/null 2>&1; then
    GLIBC_MAX=$(objdump -T "$OUT" 2>/dev/null \
        | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -V | tail -1)
    if [ -n "$GLIBC_MAX" ]; then
        if ! awk -v a="$GLIBC_MAX" 'BEGIN { split(a, p, "."); if (p[1] > 2 || (p[1] == 2 && p[2] > 36)) exit 1; exit 0 }'; then
            echo "!!! o binario exige GLIBC_$GLIBC_MAX, mas a imagem (bookworm) tem 2.36" >&2
            echo "!!! normalmente e _GNU_SOURCE ligando as redirecoes C23 (__isoc23_*);" >&2
            echo "!!! use _DEFAULT_SOURCE (ver variants/media-player/src/wm.h)" >&2
            objdump -T "$OUT" | grep -E "GLIBC_2\.(3[7-9]|[4-9][0-9])" | head -5 >&2
            exit 1
        fi
        echo "--> glibc exigida: no maximo $GLIBC_MAX (bookworm tem 2.36)"
    fi
fi

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
