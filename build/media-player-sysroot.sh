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

# Pacotes pedidos explicitamente. O resto vem da CLOSURA de dependências: os
# `.so` do bookworm referenciam libaom3, libva2, libssh-gcrypt-4, libspeex1 e
# outras, e o ld falha com "referência não definida para ..." se elas não
# estiverem no sysroot — mesmo o código não usando essas funções diretamente.
# (Isso acontece porque o linker resolve os símbolos das bibliotecas
# compartilhadas que ele carrega.) Baixar a closura é o que torna o link um
# teste de verdade em vez de um falso positivo.
DEV=(
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
    libdrm-dev libasound2-dev libfreetype-dev
)
RUNTIME=(
    libavformat59 libavcodec59 libavutil57 libswscale6 libswresample4
    libdrm2 libasound2 libfreetype6
)

mkdir -p "$DESTINO"
CACHE="$DESTINO/.debs"
mkdir -p "$CACHE"

echo "--> baixando o índice $SUITE/$ARCH"
curl -fsSL "$MIRROR/dists/$SUITE/main/binary-$ARCH/Packages.gz" -o "$CACHE/Packages.gz"
gzip -dc "$CACHE/Packages.gz" > "$CACHE/Packages.txt"

# Pacotes da closura que NÃO devem ser extraídos: glibc e cabeçalhos de kernel
# do ALVO. O compilador cruzado já traz os dele; extrair estes faz
# /usr/include/stdio.h (do alvo) vencer o do toolchain e a mistura de versões
# quebra a compilação ("L_tmpnam undeclared", "bits/errno.h"...). O link do
# libc também é do toolchain — ver a limpeza dos ld scripts mais abaixo.
PULAR=" libc6 libc6-dev libc6-dev-arm64-cross linux-libc-dev \
        linux-libc-dev-arm64-cross libcrypt-dev libcrypt1 libnsl-dev \
        libnsl2 rpcsvc-proto debconf debconf-i18n shared-mime-info "

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
    case "$PULAR" in
        *" $p "*)
            # Baixado (para a closura continuar sendo resolvida), mas não
            # extraído — ver o comentário de PULAR.
            return 0
            ;;
    esac
    dpkg-deb -x "$deb" "$DESTINO"
}

# Mapa pacote -> Depends, para resolver a closura sem chamar o apt.
awk 'BEGIN { RS=""; FS="\n" }
     { pkg=""; dep="";
       for (i=1;i<=NF;i++) {
         if ($i ~ /^Package: /) { pkg=substr($i,10) }
         else if ($i ~ /^Depends: /) { dep=substr($i,10) }
       }
       if (pkg != "") printf "%s\t%s\n", pkg, dep }' \
    "$CACHE/Packages.txt" > "$CACHE/deps.txt"

# Nome do primeiro pacote de cada alternativa, sem versão/arquitetura.
sem_resto() {
    printf '%s\n' "$1" \
        | sed -e 's/|.*//' -e 's/([^)]*)//g' -e 's/\[[^]]*\]//g' \
              -e 's/<[^>]*>//g' -e 's/:any//g' -e 's/[[:space:]]//g'
}

echo "--> resolvendo a closura de dependências"
declare -A VISTO=()
FILA=("${DEV[@]}" "${RUNTIME[@]}")
FALTANDO=0
while [ ${#FILA[@]} -gt 0 ]; do
    p=${FILA[0]}
    FILA=("${FILA[@]:1}")
    p=$(sem_resto "$p")
    if [ -z "$p" ] || [ -n "${VISTO[$p]:-}" ]; then
        continue
    fi
    VISTO[$p]=1

    if ! grep -q "^Package: $p$" "$CACHE/Packages.txt"; then
        # Pacote virtual (Provides) ou de outra seção: não é erro, só não entra.
        continue
    fi
    baixar "$p" >/dev/null || FALTANDO=$((FALTANDO+1))

    # `|| true`: com set -e, um grep sem resultado (pacote sem Depends, caso de
    # libc6) derrubaria o script em silêncio. Também não usar `grep -P`: nem todo
    # grep tem PCRE.
    deps=$(awk -v alvo="$p" -F'\t' '$1 == alvo { print $2; exit }' "$CACHE/deps.txt" || true)
    if [ -z "$deps" ]; then
        continue
    fi
    IFS=',' read -ra PARTES <<< "$deps"
    for item in "${PARTES[@]}"; do
        FILA+=("$item")
    done
done

if [ "$FALTANDO" -gt 0 ]; then
    echo "!!! $FALTANDO pacote(s) da closura nao puderam ser baixados" >&2
    exit 1
fi
echo "    ${#VISTO[@]} pacotes na closura"

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

# O glibc do alvo NÃO pode ficar neste diretório: o `libc.so` de lá é um ld
# script com caminhos ABSOLUTOS (/lib/aarch64-linux-gnu/libc.so.6), e como os
# `-L` do sysroot vêm antes dos diretórios do cross-compiler, o ld pegaria esse
# script e falharia com "não foi possível localizar /lib/aarch64-linux-gnu/...".
# glibc e o loader corretos são os do próprio cross-compiler; aqui só precisamos
# das bibliotecas da aplicação (FFmpeg, ALSA, FreeType, libdrm).
for libdir in "$DESTINO"/usr/lib/aarch64-linux-gnu "$DESTINO"/lib/aarch64-linux-gnu; do
    [ -d "$libdir" ] || continue
    rm -f "$libdir"/libc.so "$libdir"/libc_nonshared.a \
          "$libdir"/libm.so "$libdir"/libdl.so "$libdir"/libpthread.so \
          "$libdir"/librt.so "$libdir"/libresolv.so "$libdir"/libutil.so \
          "$libdir"/libanl.so "$libdir"/libmvec.so \
          "$libdir"/ld-linux-aarch64.so.1 "$libdir"/ld-2.*.so
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
