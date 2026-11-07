#!/usr/bin/env bash
# build-local-rk3326.sh — constrói a imagem do R35S/R36S (RK3326) NESTA máquina,
# sem depender do GitHub Actions.
#
# Por que existe: a linha RK3326 do CI só roda no botão (ou por tag `rk3326-*`),
# e gerar a imagem para gravar não pode ficar refém de um token. Este script
# reproduz os MESMOS passos de `.github/workflows/build-rk3326.yml`, com os
# mesmos valores padrão (loader=oficial, kernelSource=proprio, v7.1). Se um dia
# divergirem, o workflow é a referência.
#
# Como funciona: o host só precisa de `docker`. Todo o trabalho acontece num
# container Ubuntu 24.04 com `--privileged -u 0` (precisa de loop device e mount
# para montar a imagem). O repositório entra SOMENTE LEITURA em /src e é copiado
# para /work lá dentro; só o diretório de saída vem do host. Assim a árvore de
# trabalho não é sujada com kernel/, rootfs/, uboot/ nem img/.
#
# Se o build falhar, o container FICA de pé com o que já foi feito (kernel
# compilado, rootfs) e a próxima chamada continua de onde parou — só o
# repositório é recopiado. Para começar do zero: `docker rm -f` no container.
#
# Uso:
#   tools/build-local-rk3326.sh                    # media-player
#   tools/build-local-rk3326.sh --variante kodi
#   tools/build-local-rk3326.sh --variante retroarch
#   tools/build-local-rk3326.sh --saida /caminho   # onde deixar o .img.gz
#   tools/build-local-rk3326.sh --reaproveitar     # mantém o container mesmo
#                                                  # depois do sucesso
#
# Requisitos: docker, ~25 GB livres, internet (baixa 2 GB da ROCKNIX + kernel).
# Tempo: ~50 min na primeira vez; ~10 min retomando com o container quente.
set -euo pipefail

VARIANTE=media-player
SAIDA=""
IMAGEM_DOCKER=ubuntu:24.04
REAPROVEITAR=0
DENTRO=0
CONTAINER=ouvila-rk3326-build
KERNEL_VERSION=${KERNEL_VERSION:-v7.1}

uso() {
    sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --variante)     VARIANTE=${2:?}; shift 2 ;;
        --saida)        SAIDA=${2:?}; shift 2 ;;
        --imagem)       IMAGEM_DOCKER=${2:?}; shift 2 ;;
        --reaproveitar) REAPROVEITAR=1; shift ;;
        --dentro)       DENTRO=1; shift ;;   # uso interno (chamada do host)
        -h|--help)      uso 0 ;;
        *) echo "opcao desconhecida: $1" >&2; uso 2 ;;
    esac
done

RAIZ=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SAIDA=${SAIDA:-$RAIZ/out-rk3326}

# ---------------------------------------------------------------------------
#  binfmt_misc — o chroot arm64 do debootstrap precisa que o kernel saiba
#  executar ELF aarch64 via qemu.
# ---------------------------------------------------------------------------
# ARMADILHA QUE DERRUBOU O AMBIENTE (2026-11-07): registrar o handler com
#   printf ':qemu-aarch64:M::\x7fELF\x02\x01\x01\x00...' > .../register
# grava MAGIC CURTA — o printf do bash encerra o escape no primeiro caractere
# que nao for digito hex e a linha sai com 7 bytes em vez de 20. Uma magic de
# 7 bytes casa com QUALQUER ELF de 64 bits, inclusive os do host: a partir dai
# `bash`, `ls` e `docker` passam a ser entregues ao qemu-aarch64 e a maquina
# inteira para de executar (ENOEXEC) — e desfazer exige root, que aqui nao ha.
# Por isso o registro e feito em Python (bytes exatos), conferido NA VOLTA
# (magic e mask tem de ter os 20 bytes) e DESFEITO na mesma hora se nao bater.
# O Python ja esta rodando: ele sobrevive mesmo se o exec quebrar.
garantir_binfmt() {
    local real=/usr/bin/qemu-aarch64-static
    [ -x "$real" ] || { echo "ERRO: falta $real (pacote qemu-user-static)"; exit 1; }
    if [ ! -e /proc/sys/fs/binfmt_misc/register ]; then
        mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
    fi
    python3 - "$real" <<'PY'
import os, sys

real = sys.argv[1]
reg = "/proc/sys/fs/binfmt_misc/register"
ent = "/proc/sys/fs/binfmt_misc/qemu-aarch64"

# ELF64, little-endian, EM_AARCH64 (0xb7) — os mesmos 20 bytes do qemu-binfmt-conf.sh.
magic = bytes.fromhex("7f454c460201010000000000000000000200b700")
mask = bytes.fromhex("ffffffffffffff00fffffffffffffffeffffff")
if len(magic) != 20 or len(mask) != 20:
    raise SystemExit("magic/mask com tamanho errado")


def esc(b):
    return "".join("\\x%02x" % x for x in b)


def texto():
    try:
        with open(ent) as f:
            return f.read()
    except OSError:
        return ""


def saudavel(t):
    return ("magic " + magic.hex()) in t and ("mask " + mask.hex()) in t


def desfazer():
    try:
        fd = os.open(ent, os.O_WRONLY)
        os.write(fd, b"-1\n")
        os.close(fd)
    except OSError:
        pass


t = texto()
if t and not saudavel(t):
    print("binfmt: entrada qemu-aarch64 existente e invalida; removendo")
    desfazer()
    t = texto()

if not saudavel(t):
    linha = ":qemu-aarch64:M::" + esc(magic) + ":" + esc(mask) + ":" + real + ":OCF"
    fd = os.open(reg, os.O_WRONLY)
    os.write(fd, (linha + "\n").encode())
    os.close(fd)
    t = texto()
    if not saudavel(t):
        print("!!! binfmt: o registro nao conferiu com os 20 bytes esperados:")
        print(t)
        desfazer()
        sys.exit(1)
print("binfmt: qemu-aarch64 registrado (magic/mask de 20 bytes conferidos)")
PY
}

# Devolve o ambiente como estava: no fim do build nao fica handler nenhum.
desfazer_binfmt() {
    python3 - <<'PY' || true
try:
    import os
    fd = os.open("/proc/sys/fs/binfmt_misc/qemu-aarch64", os.O_WRONLY)
    os.write(fd, b"-1\n")
    os.close(fd)
    print("binfmt: qemu-aarch64 removido")
except OSError:
    pass
PY
}

# ============================================================================
#  Dentro do container (root, /work tem o repositório).
# ============================================================================
dentro() {
    local log=/out/build-local.log
    exec > >(tee -a "$log") 2>&1
    set -x
    cd /work

    export DEBIAN_FRONTEND=noninteractive
    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-

    echo "=== [1/8] dependencias"
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        build-essential gcc-aarch64-linux-gnu bison flex libssl-dev bc swig \
        libgnutls28-dev uuid-dev device-tree-compiler python3 python3-dev \
        python3-setuptools python3-pyelftools debootstrap qemu-user-static \
        parted kpartx dosfstools e2fsprogs rsync ca-certificates curl git xz-utils \
        >/dev/null
    echo "    ok"

    echo "=== [2/8] imagem da ROCKNIX (fonte do loader oficial)"
    local url="https://github.com/ROCKNIX/distribution/releases/download/20261001/ROCKNIX-RK3326.aarch64-20261001-a.img.gz"
    local sha="d693d2bb3cd854a54a1c742c9952f20892c8344188c696beea00c77b5bd54392"
    # Sao ~1,3 GB vindos de release-assets.githubusercontent.com e a conexao cai no
    # meio (foi um "(56) Recv failure" que derrubou o build de 2026-11-06). Duas
    # coisas sao obrigatorias aqui:
    #   - `-C -` para RETOMAR (sem isso cada tentativa recomeca do zero);
    #   - `--retry-all-errors`, porque o `--retry` sozinho NAO repete `(56)`.
    # O destino e o cache em /out, que sobrevive ao `docker rm`; a copia em /work e
    # o que o resto do script abre.
    #
    # ARMADILHA (2026-11-06): o codigo antigo era `elif [ ! -f rocknix.img.gz ]`.
    # Com um parcial de 946 MB em /work ele PULAVA o download e morria no
    # sha256sum -c. Agora o arquivo existente e sempre retomado, nunca ignorado.
    mkdir -p /out/cache
    local cache=/out/cache/rocknix.img.gz
    local tentativa obtidos
    for tentativa in 1 2 3 4; do
        if [ -f "$cache" ] && echo "$sha  $cache" | sha256sum -c - >/dev/null 2>&1; then
            echo "    cache: imagem da ROCKNIX ja integra em $cache"
            break
        fi
        if [ "$tentativa" = 4 ]; then
            # Tres retomadas falharam no checksum: o que veio esta corrompido do meio
            # para tras e nao da para continuar em cima. Recomeca do zero uma vez.
            echo "    sha256 nao confere depois de 3 tentativas; descartando e baixando do zero"
            rm -f "$cache"
        fi
        obtidos=0
        if [ -f "$cache" ]; then
            obtidos=$(( $(stat -c%s "$cache") / 1048576 ))
        fi
        echo "    imagem da ROCKNIX: tentativa $tentativa (${obtidos} MB ja obtidos)"
        curl -fL -C - --retry 8 --retry-delay 5 --retry-all-errors \
            --connect-timeout 30 --speed-limit 1024 --speed-time 60 \
            -o "$cache" "$url" ||
            echo "    !! curl parou (status $?); o parcial ficou em $cache"
    done
    echo "$sha  $cache" | sha256sum -c -
    cp -f "$cache" rocknix.img.gz
    mkdir -p uboot
    python3 - <<'PY'
import gzip
LIMITE = 16 * 1024 * 1024
buf = bytearray()
with gzip.open("rocknix.img.gz", "rb") as f:
    while len(buf) < LIMITE:
        b = f.read(1 << 20)
        if not b:
            break
        buf += b
if len(buf) < LIMITE:
    raise SystemExit("imagem menor que 16 MiB: %d" % len(buf))
regiao = bytes(buf[:LIMITE])
open("uboot/idbloader.img", "wb").write(regiao[32768:8388608])
open("uboot/u-boot.itb", "wb").write(regiao[8388608:16777216])
print("idbloader.img %d bytes | u-boot.itb %d bytes"
      % (8388608 - 32768, 16777216 - 8388608))
PY

    echo "=== [3/8] kernel $KERNEL_VERSION + drivers da ROCKNIX"
    local k=/work/kernel
    if [ ! -f "$k/arch/arm64/boot/Image" ]; then
        if [ ! -d "$k/.git" ]; then
            rm -rf "$k"
            git clone -q --depth 1 --branch "$KERNEL_VERSION" \
                https://github.com/torvalds/linux.git "$k"
        fi

        local rocknix_commit=8c081eb645f8f9a9fc59fe0c8c7927ce79d5192c
        [ -d rocknix-src/.git ] || {
            git init -q rocknix-src
            git -C rocknix-src remote add origin https://github.com/ROCKNIX/distribution.git
        }
        git -C rocknix-src fetch -q --depth 1 origin "$rocknix_commit"
        git -C rocknix-src checkout -q --detach FETCH_HEAD
        local generic_dsi
        generic_dsi=$(find rocknix-src -iname 'panel-generic-dsi.c' | head -n1)
        [ -n "$generic_dsi" ] || { echo "ERRO: panel-generic-dsi.c ausente"; exit 1; }

        local joypad_commit=d02ed13aae08113f6f9e0e9d699cb29bb3450fa2
        [ -d joypad-src/.git ] || {
            git init -q joypad-src
            git -C joypad-src remote add origin https://github.com/ROCKNIX/rocknix-joypad.git
        }
        git -C joypad-src fetch -q --depth 1 origin "$joypad_commit"
        git -C joypad-src checkout -q --detach FETCH_HEAD
        [ -f joypad-src/rocknix-singleadc-joypad.c ] || {
            echo "ERRO: rocknix-singleadc-joypad.c ausente"; exit 1; }

        cp "$generic_dsi" "$k/drivers/gpu/drm/panel/panel-generic-dsi.c"
        grep -q 'panel-generic-dsi' "$k/drivers/gpu/drm/panel/Makefile" \
            || printf 'obj-y += panel-generic-dsi.o\n' >> "$k/drivers/gpu/drm/panel/Makefile"
        cp joypad-src/rocknix-singleadc-joypad.c "$k/drivers/input/joystick/"
        grep -q 'rocknix-singleadc-joypad' "$k/drivers/input/joystick/Makefile" \
            || printf 'obj-m += rocknix-singleadc-joypad.o\n' >> "$k/drivers/input/joystick/Makefile"

        make -s -C "$k" defconfig
        # Mesmos simbolos do workflow (nomes conferidos no Kconfig do v6.12/v7.1).
        "$k/scripts/config" --file "$k/.config" \
            -e ARCH_ROCKCHIP -e DRM -e ROCKCHIP_IOMMU -e DRM_ROCKCHIP \
            -e ROCKCHIP_DW_MIPI_DSI -e DRM_PANEL_SIMPLE -e BACKLIGHT_PWM \
            -e PWM_ROCKCHIP -e MFD_RK8XX_I2C -e REGULATOR_RK808 -e CHARGER_RK817 \
            -e ROCKCHIP_SARADC -e SND_SOC_ROCKCHIP -e SND_SOC_ROCKCHIP_I2S \
            -e SND_SOC_RK817 -e PINCTRL_ROCKCHIP -e I2C_RK3X -e MMC_DW \
            -e MMC_DW_ROCKCHIP -e SERIAL_8250_DW -e USB_DWC2 \
            -e PHY_ROCKCHIP_INNO_USB2 -e ARM_ROCKCHIP_CPUFREQ -e ROCKCHIP_IODOMAIN \
            -e ROCKCHIP_THERMAL -e SND_SIMPLE_CARD -e INPUT_EVDEV \
            -e VT -e FRAMEBUFFER_CONSOLE -e DRM_FBDEV_EMULATION \
            -e DRM_PANEL_SITRONIX_ST7703 -e DRM_PANEL_ELIDA_KD35T133
        make -s -C "$k" olddefconfig

        echo "--- valores na config final ---"
        local sym valor falhas=0
        for sym in ARCH_ROCKCHIP DRM DRM_ROCKCHIP ROCKCHIP_IOMMU ROCKCHIP_DW_MIPI_DSI \
                   DRM_PANEL_SIMPLE MFD_RK8XX_I2C REGULATOR_RK808 ROCKCHIP_SARADC \
                   SND_SOC_ROCKCHIP_I2S SND_SOC_RK817 MMC_DW_ROCKCHIP PWM_ROCKCHIP \
                   I2C_RK3X SERIAL_8250_DW USB_DWC2; do
            valor=$(sed -n "s/^CONFIG_${sym}=//p" "$k/.config")
            printf '  %-32s [%s]\n' "$sym" "${valor:-AUSENTE}"
        done
        for sym in ARCH_ROCKCHIP DRM_ROCKCHIP MFD_RK8XX_I2C ROCKCHIP_SARADC \
                   MMC_DW_ROCKCHIP PWM_ROCKCHIP I2C_RK3X REGULATOR_RK808 SND_SOC_RK817 \
                   FRAMEBUFFER_CONSOLE DRM_FBDEV_EMULATION; do
            if ! grep -qE "^CONFIG_${sym}=(y|m)$" "$k/.config"; then
                echo "ERRO: CONFIG_${sym} nao ficou ligado"
                falhas=$((falhas + 1))
            fi
        done
        if [ "$falhas" -ne 0 ]; then
            grep -E "ROCKCHIP|PX30|SARADC|RK8XX" "$k/.config" || true
            exit 1
        fi

        make -s -C "$k" -j"$(nproc)" Image modules
    fi
    [ -f "$k/arch/arm64/boot/Image" ] || { echo "ERRO: Image ausente"; exit 1; }
    echo "    Image: $(stat -c%s "$k/arch/arm64/boot/Image") bytes | modulos: $(find "$k" -name '*.ko' | wc -l)"

    echo "=== [4/8] DTB + overlay de painel"
    mkdir -p dtbs/overlays
    cp devices/game-console-r35s/dts/rk3326-gameconsole-r36s.dtb dtbs/
    dtc -@ -O dtb -o dtbs/overlays/mipi-panel.dtbo \
        devices/game-console-r35s/dts/panel-overlay/mipi-panel.dts
    dtc -I dtb -O dts dtbs/overlays/mipi-panel.dtbo > /dev/null || {
        echo "ERRO: o overlay compilado nao reabre"; exit 1; }

    echo "=== [5/8] rootfs Debian arm64"
    garantir_binfmt
    trap desfazer_binfmt EXIT
    if [ ! -f rootfs/.debootstrap-ok ]; then
        rm -rf rootfs
        debootstrap --arch=arm64 --foreign --variant=minbase \
            bookworm rootfs http://deb.debian.org/debian
        cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
        chroot rootfs /debootstrap/debootstrap --second-stage
        touch rootfs/.debootstrap-ok
    fi
    cat > rootfs/etc/apt/sources.list <<'EOF'
deb http://deb.debian.org/debian bookworm main contrib non-free-firmware
deb http://deb.debian.org/debian bookworm-updates main contrib non-free-firmware
deb http://security.debian.org/debian-security bookworm-security main contrib non-free-firmware
EOF

    local pacotes
    pacotes="$(grep -hv '^[[:space:]]*#' common/packages/base.list \
                 "variants/$VARIANTE/packages/$VARIANTE.list" | tr '\n' ' ')"
    echo "    pacotes: $pacotes"
    chroot rootfs apt-get update -qq
    chroot rootfs env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        --no-install-recommends $pacotes

    cp -a common/overlay/. rootfs/
    cp -a "variants/$VARIANTE/overlay/." rootfs/
    make -s -C "$k" INSTALL_MOD_PATH=/work/rootfs modules_install

    mkdir -p rootfs/etc/ssh/sshd_config.d
    cat > rootfs/etc/ssh/sshd_config.d/10-ouvila.conf <<'EOF'
PermitRootLogin yes
PasswordAuthentication yes
EOF
    chroot rootfs sh -c 'echo root:root | chpasswd'

    echo "=== [6/8] app de midia (variante $VARIANTE)"
    if [ "$VARIANTE" = "media-player" ]; then
        chroot rootfs sh -c '
            for lib in libavformat libavcodec libavutil libswresample \
                       libswscale libdrm libasound libfreetype libharfbuzz; do
              ldconfig -p | grep -q "$lib\." || { echo "faltando: $lib" >&2; exit 1; }
            done
            for f in dejavu/DejaVuSans.ttf noto/NotoSansArabic-Regular.ttf \
                     noto/NotoSansDevanagari-Regular.ttf \
                     noto/NotoSansBengali-Regular.ttf wqy/wqy-zenhei.ttc; do
              [ -r "/usr/share/fonts/truetype/$f" ] || {
                echo "faltando a fonte $f" >&2; exit 1; }
            done
          '
        local sysroot=/tmp/wm-sysroot
        [ -d "$sysroot" ] || ./build/media-player-sysroot.sh "$sysroot"
        WM_SYSROOT="$sysroot" WM_OUT=build/ouvila-media ./build/media-player.sh
        install -D -m 0755 build/ouvila-media rootfs/usr/local/bin/ouvila-media
        install -D -m 0755 variants/media-player/overlay/usr/local/bin/start-media-player.sh \
            rootfs/usr/local/bin/start-media-player.sh
        install -D -m 0644 \
            variants/media-player/overlay/etc/systemd/system/media-player.service \
            rootfs/etc/systemd/system/media-player.service
        install -D -m 0644 \
            variants/media-player/overlay/etc/systemd/logind.conf.d/10-ouvila-media-power.conf \
            rootfs/etc/systemd/logind.conf.d/10-ouvila-media-power.conf
        echo "    conferindo que o binario arm64 executa (qemu)..."
        chroot rootfs /usr/local/bin/ouvila-media --version
    fi

    echo "=== [7/8] servico do tty1"
    local servico
    case $VARIANTE in
        kodi)         servico=kodi.service ;;
        media-player) servico=media-player.service ;;
        *)            servico=retroarch.service ;;
    esac
    chroot rootfs systemctl enable "$servico"
    chroot rootfs systemctl mask getty@tty1.service || true
    chroot rootfs systemctl enable serial-getty@ttyS2.service || true
    rm -f rootfs/usr/bin/qemu-aarch64-static
    desfazer_binfmt
    trap - EXIT

    echo "=== [8/8] imagem do cartao"
    mkdir -p img
    env \
        OUVILA_UBOOT_DIR=/work/uboot \
        OUVILA_KERNEL_DIR="$k" \
        OUVILA_ROOTFS_DIR=/work/rootfs \
        OUVILA_OUTPUT_DIR=/work/img \
        OUVILA_DTB_SOURCE=/work/devices/game-console-r35s/dts/rk3326-gameconsole-r36s.dtb \
        OUVILA_IMAGE_NAME="rk3326-r36s-${VARIANTE}-local-sdcard.img" \
        OUVILA_EARLYCON=0xff160000 \
        OUVILA_DTBS_GLOB="rk3326-*.dtb" \
        OUVILA_FDT_DIR= \
        OUVILA_LABEL=ouvila \
        OUVILA_PANEL_OVERLAY_NAME=mipi-panel.dtbo \
        OUVILA_PANEL_OVERLAY_FILE=/work/dtbs/overlays/mipi-panel.dtbo \
        build/build.sh game-console-r35s "$VARIANTE"

    local f
    for f in img/*.img.gz; do
        [ -f "$f" ] || continue
        cp "$f" /out/
        ( cd /out && sha256sum "$(basename "$f")" > "$(basename "$f").sha256" )
    done
    ls -lh /out/*.img.gz*
    echo "=== build local concluido"
}

if [ "$DENTRO" = 1 ]; then
    dentro
    exit 0
fi

# ============================================================================
#  Host: prepara o container e delega.
# ============================================================================
case $VARIANTE in
    kodi|retroarch|media-player) ;;
    *) echo "variante invalida: $VARIANTE (kodi|retroarch|media-player)" >&2; exit 2 ;;
esac

if ! docker info >/dev/null 2>&1; then
    echo "docker nao esta disponivel (sem ele nao da para montar a imagem:" >&2
    echo "loop device e mount exigem root, e o host nao tem sudo)" >&2
    exit 1
fi

mkdir -p "$SAIDA"
SAIDA=$(cd "$SAIDA" && pwd)

# O script precisa ir para dentro do container; ele já está no repositório, que
# é montado em /src. Nada é copiado do host além do diretório de saída.
echo "== Ouvila: imagem RK3326 (R35S/R36S)"
echo "   variante : $VARIANTE"
echo "   saida    : $SAIDA"
echo "   base     : $IMAGEM_DOCKER"
echo "   kernel   : $KERNEL_VERSION"
echo

# O container e SEMPRE nomeado e reaproveitado: dentro de /work ficam o kernel
# compilado (~35 min) e o rootfs. Na primeira vez ele e criado; nas seguintes o
# conteudo antigo permanece e so o repositorio e recopiado. Assim uma falha no
# meio do caminho nao joga fora o kernel. `--reaproveitar` decide apenas se o
# container fica para a proxima quando o build da certo.
if docker inspect "$CONTAINER" >/dev/null 2>&1; then
    docker start "$CONTAINER" >/dev/null
    docker exec "$CONTAINER" bash -c 'mkdir -p /work && cp -a /src/. /work/'
else
    docker run -d --name "$CONTAINER" --privileged -u 0 \
        -v "$RAIZ":/src:ro -v "$SAIDA":/out \
        "$IMAGEM_DOCKER" sleep infinity >/dev/null
    docker exec "$CONTAINER" bash -c 'mkdir -p /work && cp -a /src/. /work/'
fi

set +e
docker exec -e VARIANTE="$VARIANTE" -e KERNEL_VERSION="$KERNEL_VERSION" \
    "$CONTAINER" bash /work/tools/build-local-rk3326.sh --dentro
rc=$?
set -e

if [ "$rc" -ne 0 ]; then
    cat <<EOF

!! O build parou (status $rc). O container '$CONTAINER' ficou de pe com o que
!! ja foi feito (kernel compilado, rootfs). Depois de corrigir a causa, rode:
!!     tools/build-local-rk3326.sh --variante $VARIANTE
!! Para jogar tudo fora:  docker rm -f $CONTAINER
EOF
    exit "$rc"
fi

if [ "$REAPROVEITAR" = 1 ]; then
    echo "   (container '$CONTAINER' mantido: a proxima roda em ~10 min)"
else
    docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
fi

echo
echo "== pronto:"
ls -lh "$SAIDA"/*.img.gz* 2>/dev/null || echo "   (nada em $SAIDA)"
cat <<EOF

Gravar no cartao (confira o dispositivo duas vezes — o dd nao pergunta):
  gunzip -c "$SAIDA"/*.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress

O console do aparelho sai em ttyS2 a 1500000 8N1 (TTL).
EOF
