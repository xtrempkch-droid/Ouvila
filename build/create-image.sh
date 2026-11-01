#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 8 && $# -ne 9 ]]; then
    echo "Usage: $0 <image-file> <uboot-dir> <kernel-dir> <rootfs-dir> <device> <variant> <dtb-file> <dtb-name> [display-transform]" >&2
    exit 2
fi

IMAGE_FILE=$1
UBOOT_DIR=$2
KERNEL_DIR=$3
ROOTFS_DIR=$4
DEVICE=$5
VARIANT=$6
DTB_FILE=$7
DTB_NAME=$8
# Opcional: rotação vinda de devices/<device>/device.conf (display_transform).
# Se vier vazia, o valor é decidido pelo 'case $DEVICE' mais abaixo.
DISPLAY_TRANSFORM=${9:-}
# 3584 MiB (3,5 GiB): o eMMC destas placas tem 3776 MiB e uma imagem maior
# nao cabe nele (o dd morreria no meio e deixaria o eMMC corrompido).
# A raiz usada e ~1,3 GB. Para cartoes maiores: OUVILA_IMAGE_SIZE_MB=8192.
IMAGE_SIZE_MB=${OUVILA_IMAGE_SIZE_MB:-3584}
# Knobs por SoC/placa (default = RK3566, o comportamento historico). O RK3326
# (Game Console R35S/R36S) usa outro endereco de UART2 e outro padrao de DTB,
# e pode precisar de um overlay de painel (FDTOVERLAYS).
EARLYCON=${OUVILA_EARLYCON:-0xfe660000}
DTBS_GLOB=${OUVILA_DTBS_GLOB:-rk3566-powkiddy-*.dtb}
PANEL_OVERLAY_NAME=${OUVILA_PANEL_OVERLAY_NAME:-}
PANEL_OVERLAY_FILE=${OUVILA_PANEL_OVERLAY_FILE:-}
# Onde o DTB mora na particao de boot. Padrao 'dtbs' (subpasta, como no RK3566).
# O U-Boot de fabricante do RK3326 (2017.09) so carrega o FDT da RAIZ: para
# aquela placa use OUVILA_FDT_DIR="" . O DTB tambem e copiado para a raiz em
# qualquer caso (custa 68 KB e salva o boot).
FDT_DIR=${OUVILA_FDT_DIR-dtbs}
# Rotulo do extlinux. O parser do U-Boot de fabricante e fragil (o proprio
# README deles avisa que espaco depois do caminho do dtb quebra o boot), entao
# para o RK3326 use um rotulo simples (OUVILA_LABEL=ouvila).
LABEL_NAME=${OUVILA_LABEL-"Ouvila ($DEVICE - $VARIANT)"}
BOOT_MOUNT=
ROOT_MOUNT=
LOOP_DEV=
MOUNT_DIR=
CREATED_BOOT_DEV=0
CREATED_ROOT_DEV=0

for file in \
    "$UBOOT_DIR/idbloader.img" \
    "$UBOOT_DIR/u-boot.itb" \
    "$KERNEL_DIR/arch/arm64/boot/Image" \
    "$DTB_FILE"; do
    if [[ ! -f $file ]]; then
        echo "Required image input not found: $file" >&2
        exit 1
    fi
done
if [[ ! -d $ROOTFS_DIR ]]; then
    echo "Root filesystem directory not found: $ROOTFS_DIR" >&2
    exit 1
fi
if [[ ! $DTB_NAME =~ ^[a-zA-Z0-9._-]+\.dtb$ ]]; then
    echo "Invalid DTB destination filename: $DTB_NAME" >&2
    exit 2
fi
if [[ -n $DISPLAY_TRANSFORM && ! $DISPLAY_TRANSFORM =~ ^(auto|normal|rotate-90|rotate-180|rotate-270|flipped|flipped-rotate-90|flipped-rotate-180|flipped-rotate-270)$ ]]; then
    echo "Invalid display transform: $DISPLAY_TRANSFORM" >&2
    exit 2
fi
if [[ -e $IMAGE_FILE || -e $IMAGE_FILE.gz ]]; then
    echo "Refusing to overwrite existing image: $IMAGE_FILE[.gz]" >&2
    exit 1
fi
if [[ $EUID -ne 0 ]]; then
    exec sudo -- "$0" "$@"
fi

cleanup() {
    local exit_code=$?
    set +e
    if [[ -n ${BOOT_MOUNT:-} ]]; then
        umount "$BOOT_MOUNT" 2>/dev/null
    fi
    if [[ -n ${ROOT_MOUNT:-} ]]; then
        umount "$ROOT_MOUNT" 2>/dev/null
    fi
    if [[ -n ${LOOP_DEV:-} ]]; then
        losetup -d "$LOOP_DEV" 2>/dev/null
    fi
    if [[ ${CREATED_BOOT_DEV:-0} -eq 1 ]]; then
        rm -f -- "$BOOT_DEV"
    fi
    if [[ ${CREATED_ROOT_DEV:-0} -eq 1 ]]; then
        rm -f -- "$ROOT_DEV"
    fi
    if [[ -n ${MOUNT_DIR:-} ]]; then
        rmdir "$MOUNT_DIR/boot" "$MOUNT_DIR/rootfs" "$MOUNT_DIR" 2>/dev/null
    fi
    trap - EXIT
    exit "$exit_code"
}
trap cleanup EXIT

MOUNT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ouvila-image.XXXXXX")
BOOT_MOUNT="$MOUNT_DIR/boot"
ROOT_MOUNT="$MOUNT_DIR/rootfs"
mkdir "$BOOT_MOUNT" "$ROOT_MOUNT"

echo "Creating ${IMAGE_SIZE_MB}MB SD card image: $IMAGE_FILE"
dd if=/dev/zero of="$IMAGE_FILE" bs=1M count="$IMAGE_SIZE_MB" status=progress
parted -s "$IMAGE_FILE" mklabel msdos
parted -s "$IMAGE_FILE" mkpart primary fat32 16MiB 144MiB
parted -s "$IMAGE_FILE" mkpart primary ext4 144MiB 100%

LOOP_DEV=$(losetup --find --partscan --show "$IMAGE_FILE")
BOOT_DEV="${LOOP_DEV}p1"
ROOT_DEV="${LOOP_DEV}p2"
for _ in {1..20}; do
    [[ -b $BOOT_DEV && -b $ROOT_DEV ]] && break
    sleep 0.25
done
create_partition_device() {
    local partition=$1
    local device=$2
    local sysfs_dev="/sys/block/${LOOP_DEV##*/}/${LOOP_DEV##*/}p${partition}/dev"
    local major
    local minor

    [[ -b $device ]] && return 0
    if [[ ! -r $sysfs_dev ]]; then
        return 1
    fi

    IFS=: read -r major minor < "$sysfs_dev"
    mknod -- "$device" b "$major" "$minor"
    [[ -b $device ]]
}

if [[ ! -b $BOOT_DEV ]]; then
    create_partition_device 1 "$BOOT_DEV" && CREATED_BOOT_DEV=1
fi
if [[ ! -b $ROOT_DEV ]]; then
    create_partition_device 2 "$ROOT_DEV" && CREATED_ROOT_DEV=1
fi
if [[ ! -b $BOOT_DEV || ! -b $ROOT_DEV ]]; then
    echo "Partition devices were not created for $LOOP_DEV." >&2
    exit 1
fi

mkfs.vfat -F 32 -n BOOT "$BOOT_DEV"
mkfs.ext4 -F -L ROOTFS "$ROOT_DEV"

# root= pelo PARTUUID, nao por /dev/mmcblk1p2: a mesma imagem pode estar no
# eMMC (mmcblk0), no SD1 (mmcblk1) ou no SD2 (mmcblk2), e o nome muda.
# O PARTUUID acompanha a imagem, entao o mesmo extlinux.conf serve nas tres.
#
# O PARTUUID de uma tabela DOS/MBR e "<assinatura do disco>-<NN>", e a assinatura
# sai do proprio arquivo (sfdisk --disk-id), sem abrir o dispositivo de loop:
# rodar `blkid` na particao do loop logo apos o mkfs fazia o kernel/udev reler a
# tabela e derrubar /dev/loopNp1 (run #124 do CI falhou exatamente assim, com
# "mount: /mnt/boot: special device /dev/loop0p1 does not exist").
DISK_ID=$(sfdisk --disk-id "$IMAGE_FILE" 2>/dev/null | tr -d '[:space:]')
DISK_ID=${DISK_ID#0x}
if [[ ! $DISK_ID =~ ^[0-9a-fA-F]{8}$ || $DISK_ID == 00000000 ]]; then
    echo "Assinatura de disco inválida em $IMAGE_FILE: '$DISK_ID'" >&2
    exit 1
fi
ROOT_UUID="$DISK_ID-02"
echo "root=PARTUUID=$ROOT_UUID"
dd if="$UBOOT_DIR/idbloader.img" of="$LOOP_DEV" seek=64 conv=notrunc,fsync
dd if="$UBOOT_DIR/u-boot.itb" of="$LOOP_DEV" seek=16384 conv=notrunc,fsync

# O dd no dispositivo inteiro pode fazer o kernel/udev reler a tabela e derrubar
# /dev/loopNpN. Espera os nós voltarem (e recria pelo major:minor, se preciso)
# antes de montar.
for _ in {1..10}; do
    [[ -b $BOOT_DEV && -b $ROOT_DEV ]] && break
    partprobe "$LOOP_DEV" 2>/dev/null || true
    create_partition_device 1 "$BOOT_DEV" || true
    create_partition_device 2 "$ROOT_DEV" || true
    sleep 1
done
if [[ ! -b $BOOT_DEV || ! -b $ROOT_DEV ]]; then
    echo "Partições do loop não apareceram: $BOOT_DEV / $ROOT_DEV" >&2
    exit 1
fi

mount "$BOOT_DEV" "$BOOT_MOUNT"
mount "$ROOT_DEV" "$ROOT_MOUNT"

# Confere o root= que vai no extlinux contra a partição recém-formatada.
_real_partuuid=$(blkid -s PARTUUID -o value "$ROOT_DEV" 2>/dev/null || true)
if [[ -n $_real_partuuid && $_real_partuuid != "$ROOT_UUID" ]]; then
    echo "root=PARTUUID=$ROOT_UUID não bate com a partição ($_real_partuuid)" >&2
    exit 1
fi
cp "$KERNEL_DIR/arch/arm64/boot/Image" "$BOOT_MOUNT/Image"
mkdir -p "$BOOT_MOUNT/extlinux"
if [[ -n $FDT_DIR ]]; then
    mkdir -p "$BOOT_MOUNT/$FDT_DIR"
    FDT_PATH="/$FDT_DIR/$DTB_NAME"
    for dtb in "$KERNEL_DIR"/arch/arm64/boot/dts/rockchip/$DTBS_GLOB; do
        [[ -f $dtb ]] && cp "$dtb" "$BOOT_MOUNT/$FDT_DIR/"
    done
else
    FDT_PATH="/$DTB_NAME"
fi
cp "$DTB_FILE" "$BOOT_MOUNT$FDT_PATH"
# copia tambem na raiz: o U-Boot de fabricante do RK3326 carrega o FDT da raiz
cp "$DTB_FILE" "$BOOT_MOUNT/$DTB_NAME"

# Overlay de painel opcional (RK3326: o DTB base nao descreve a sequencia de
# init do painel; o overlay aplica a variante certa sem recompilar o DTB).
OVERLAY_LINE=
if [[ -n $PANEL_OVERLAY_NAME ]]; then
    if [[ -z $PANEL_OVERLAY_FILE || ! -f $PANEL_OVERLAY_FILE ]]; then
        echo "Overlay de painel pedido mas nao encontrado: $PANEL_OVERLAY_FILE" >&2
        exit 1
    fi
    mkdir -p "$BOOT_MOUNT/overlays"
    cp "$PANEL_OVERLAY_FILE" "$BOOT_MOUNT/overlays/$PANEL_OVERLAY_NAME"
    OVERLAY_LINE="
    FDTOVERLAYS /overlays/$PANEL_OVERLAY_NAME"
fi

# Rotação da interface Kodi/Weston na saída DSI-1. O valor fica em
# ouvila.conf, na partição FAT (BOOT): dá para editar no PC e testar outro
# valor sem recompilar. Valores válidos do Weston: normal, rotate-90,
# rotate-180, rotate-270, flipped, flipped-rotate-90, flipped-rotate-180,
# flipped-rotate-270 (não existe "270" sozinho).
# Fallback por nome do aparelho, usado só quando device.conf não define
# display_transform (ou quando o create-image.sh é chamado direto).
if [[ -z $DISPLAY_TRANSFORM ]]; then
    case $DEVICE in
        powkiddy-x55)
            # Painel 720x1280 (retrato) em aparelho horizontal. Sentido NÃO validado
            # no hardware: se a imagem sair de cabeça para baixo, troque por rotate-90.
            DISPLAY_TRANSFORM=rotate-90
            ;;
        powkiddy-x35s|powkiddy-x35h)
            # Painel 640x480 com rotation = <0> no DTS; mesmo DTB nos dois.
            DISPLAY_TRANSFORM=normal
            ;;
        *)
            DISPLAY_TRANSFORM=normal
            ;;
    esac
fi
cat > "$BOOT_MOUNT/ouvila.conf" << CONF
# Configuração editável do Ouvila. Lida no boot por start-kodi-wayland.sh.
# display_transform: auto | normal | rotate-90 | rotate-180 | rotate-270 |
#                    flipped | flipped-rotate-90 | flipped-rotate-180 |
#                    flipped-rotate-270
# "auto" escolhe pelo modelo (X55 -> rotate-90, X35S/X35H -> normal).
display_transform=$DISPLAY_TRANSFORM
# retroarch_exit: o que fazer quando o RetroArch é fechado pelo menu (o 1.14
# não tem item de desligar). poweroff = desliga o aparelho; stay = só sai.
retroarch_exit=poweroff
# --- variante media-player (app de mídia único) ---
# media_volume: volume inicial (0..100)
# media_audio_out: dispositivo ALSA ("default", "hw:0,0") ou "-" para desligar
# media_roots: pastas de mídia separadas por ':'
# media_exit: o que fazer quando a UI termina (poweroff ou stay)
media_volume=70
media_audio_out=default
media_roots=/storage/media:/boot/media:/storage/sdcard1/media:/storage/sdcard2/media
media_exit=poweroff
CONF

# Mídia na partição FAT: é o único lugar que se lê de um PC sem rede.
if [[ $VARIANT == media-player ]]; then
    mkdir -p "$BOOT_MOUNT/media/music" "$BOOT_MOUNT/media/videos"
    cat > "$BOOT_MOUNT/media/LEIA-ME.txt" << 'LEIAME'
MÍDIA: copie as pastas aqui na partição BOOT (no PC), assim:
  media/music/    suas músicas (mp3, flac, ogg, m4a, wav...)
  media/videos/   seus vídeos  (mp4, mkv, webm, avi...)

A ordem que o app procura (a primeira que existir manda):
  /storage/media   (ext4, via Wi-Fi + scp — melhor para muita coisa)
  /boot/media      (esta pasta: lê no PC, sem precisar de rede)
  /storage/sdcard1/media e /storage/sdcard2/media (cartão de dados)

Tags: ID3v2/ID3v1 (mp3) e Vorbis comment (flac) são lidas; sem tag ele usa
"Artista - Título.mp3". A capa é folder.jpg/cover.png na pasta do álbum.

Limite: esta partição tem 128 MB no total (kernel e dtbs ocupam parte).

Controle: A abre, B volta, L/R trocam de aba, X toca/pausa, Y menu.
LEIAME
fi

X35S_DTB="$KERNEL_DIR/arch/arm64/boot/dts/rockchip/rk3566-powkiddy-x35s.dtb"
if [[ $DEVICE == powkiddy-x35s && $(basename -- "$DTB_FILE") == rocknix-official-x35s.dtb ]]; then
    cp "$DTB_FILE" "$BOOT_MOUNT/dtbs/rk3566-powkiddy-x35s-rocknix.dtb"
    cat > "$BOOT_MOUNT/extlinux/extlinux.conf.x35s-rocknix" << BOOTCFG
LABEL Mainline (X35S - DTB ROCKNIX)
    KERNEL /Image
    FDT /dtbs/rk3566-powkiddy-x35s-rocknix.dtb
    APPEND quiet splash vt.global_cursor_default=0 rootwait earlycon=uart8250,mmio32,$EARLYCON console=ttyS2,1500000n8 console=tty1 root=PARTUUID=$ROOT_UUID rw
BOOTCFG
fi
if [[ $DEVICE == powkiddy-x35s && -f $X35S_DTB ]]; then
    cp "$X35S_DTB" "$BOOT_MOUNT/dtbs/rk3566-powkiddy-x35s-nosso.dtb"
    cat > "$BOOT_MOUNT/extlinux/extlinux.conf.x35s-nosso" << BOOTCFG
LABEL Mainline (X35S - nosso .dtb)
    KERNEL /Image
    FDT /dtbs/rk3566-powkiddy-x35s-nosso.dtb
    APPEND quiet splash vt.global_cursor_default=0 rootwait earlycon=uart8250,mmio32,$EARLYCON console=ttyS2,1500000n8 console=tty1 root=PARTUUID=$ROOT_UUID rw
BOOTCFG
fi

if [[ -f $BOOT_MOUNT/dtbs/rk3566-powkiddy-x55.dtb ]]; then
    cat > "$BOOT_MOUNT/extlinux/extlinux.conf.x55" << BOOTCFG
LABEL Mainline (Powkiddy X55)
    KERNEL /Image
    FDT /dtbs/rk3566-powkiddy-x55.dtb
    APPEND quiet splash vt.global_cursor_default=0 rootwait earlycon=uart8250,mmio32,$EARLYCON console=ttyS2,1500000n8 console=tty1 root=PARTUUID=$ROOT_UUID rw ouvila_x55_gamepad=rocknix-v1
BOOTCFG
fi

BOOT_MARKER=
if [[ $DEVICE == powkiddy-x55 ]]; then
    BOOT_MARKER=" ouvila_x55_gamepad=rocknix-v1"
fi

cat > "$BOOT_MOUNT/extlinux/extlinux.conf" << BOOTCFG
LABEL $LABEL_NAME
    KERNEL /Image
    FDT $FDT_PATH$OVERLAY_LINE
    APPEND quiet splash vt.global_cursor_default=0 rootwait earlycon=uart8250,mmio32,$EARLYCON console=ttyS2,1500000n8 console=tty1 root=PARTUUID=$ROOT_UUID rw$BOOT_MARKER
BOOTCFG

cp -a "$ROOTFS_DIR"/. "$ROOT_MOUNT"/
umount "$BOOT_MOUNT"
BOOT_MOUNT=
umount "$ROOT_MOUNT"
ROOT_MOUNT=
losetup -d "$LOOP_DEV"
LOOP_DEV=
rmdir "$MOUNT_DIR/boot" "$MOUNT_DIR/rootfs" "$MOUNT_DIR"
MOUNT_DIR=
trap - EXIT

gzip -f -- "$IMAGE_FILE"
echo "Created image: $IMAGE_FILE.gz"
