#!/bin/sh
# Armazenamento extra: usa os slots SD que NAO tem o sistema.
#
# Nas X35S/X35H existem TRES midias (medido no aparelho):
#   mmc@fe310000 (mmc0) = eMMC  3,7 GiB   -> sdhci-dwcmshc
#   mmc@fe2b0000 (mmc1) = SD1              -> dwmmc_rockchip
#   mmc@fe000000 (mmc2) = SD2              -> dwmmc_rockchip
#   mmc@fe2c0000 (mmc3) = SDIO do radio (nao e slot de cartao)
#
# O sistema pode estar em qualquer uma das tres. As outras viram area de dados:
# montadas em /storage/sdcard<N> e ligadas em /boot/roms/sdcard<N> (a partição
# FAT, que o navegador de arquivos do RetroArch ja abre).
#
# POR QUE O RESCAN: o DTB em uso (o oficial da ROCKNIX, no caso da X35H) nao
# declara cd-gpios, entao o MMC core marca os slots como removaveis=false e nao
# percebe cartao inserido depois do boot. O driver dwmmc_rockchip, porem,
# re-detecta o cartao ao ser religado (unbind/bind), verificado no aparelho:
#   echo fe000000.mmc > /sys/bus/platform/drivers/dwmmc_rockchip/unbind
#   echo fe000000.mmc > /sys/bus/platform/drivers/dwmmc_rockchip/bind
# Isso funciona com qualquer DTB (nao exige recompilar o kernel) e e a unica
# forma de ter hotplug sem inventar GPIO de card-detect.
#
# NUNCA religa o host que tem o sistema montado: derrubaria o proprio root.
set -u

INTERVALO=${OUVILA_STORAGE_INTERVALO:-5}   # segundos entre uma varredura e outra
COOLDOWN=${OUVILA_STORAGE_COOLDOWN:-15}    # espera entre rescans do mesmo slot
LOG=${OUVILA_STORAGE_LOG:-/boot/extra-storage.log}

# Slots de cartao (o eMMC fica de fora: e nao removivel e costuma ter o sistema).
# OUVILA_STORAGE_SLOTS existe para testar um slot isolado no aparelho.
SLOTS=${OUVILA_STORAGE_SLOTS:-"fe2b0000.mmc fe000000.mmc"}
DRIVER=dwmmc_rockchip

# "on" (padrao) religa o slot para detectar cartao inserido com o aparelho
# ligado; "off" so monta o que ja apareceu no boot.
HOTPLUG=on
if [ -r "${OUVILA_CONF:-/boot/ouvila.conf}" ]; then
    _v=$(sed -n 's/^[[:space:]]*extra_storage_hotplug[[:space:]]*=[[:space:]]*//p' \
        "${OUVILA_CONF:-/boot/ouvila.conf}" | tail -n 1 | tr -d '\r[:space:]')
    [ -n "$_v" ] && HOTPLUG=$_v
fi

# O log vive na partição FAT (legível no PC). Se /boot nao estiver gravavel
# (raro), cai no journal em vez de falhar.
_log() {
    printf '%s %s\n' "$(date -Is)" "$*" >> "$LOG" 2>/dev/null || echo "$*" >&2
}

_disk_of_host() {
    # nome do disco de bloco que esta sob o host MMC, ou vazio.
    _b=$(ls -d /sys/devices/platform/"$1"/mmc_host/*/*/block/* 2>/dev/null | head -n 1)
    [ -n "$_b" ] && basename "$_b"
}

_host_of_disk() {
    # host (fe2b0000.mmc) a que o disco pertence, ou vazio.
    [ -n "${1:-}" ] || return 0
    readlink -f "/sys/block/$1" 2>/dev/null \
        | sed -n 's|.*/platform/\([^/]*\.mmc\)/.*|\1|p'
}

_root_disk() {
    _part=$(awk '$2 == "/" { print $1; exit }' /proc/mounts)
    [ -n "${_part:-}" ] || return 0
    basename "$(readlink -f "/sys/class/block/$(basename "$_part")/.." 2>/dev/null)" 2>/dev/null
}

_fstype() { blkid -o value -s TYPE "$1" 2>/dev/null | head -n 1; }
_label()  { blkid -o value -s LABEL "$1" 2>/dev/null | head -n 1; }

# Escolhe a particao utilizavel do disco. Pula as do sistema (BOOT/ROOTFS) e a
# que ja esta montada: se o proprio cartao do sistema estiver no slot, monta-lo
# de novo escreveria na raiz viva.
_particionavel() {
    for _p in /dev/"$1"p* /dev/"$1"; do
        [ -b "$_p" ] || continue
        _fs=$(_fstype "$_p")
        case "${_fs:-}" in
            vfat|exfat|ext2|ext3|ext4|xfs|btrfs) ;;
            *) continue ;;
        esac
        case "$(_label "$_p")" in
            BOOT|ROOTFS) continue ;;
        esac
        grep -q "^$_p " /proc/mounts && continue
        printf '%s\n' "$_p"
        return 0
    done
    return 1
}

_nome_do_slot() {
    case "$1" in
        fe2b0000.mmc) echo sdcard1 ;;
        fe000000.mmc) echo sdcard2 ;;
        *)            echo sdcard ;;
    esac
}

_montar() {
    _host=$1; _disco=$2
    _nome=$(_nome_do_slot "$_host")
    _part=$(_particionavel "$_disco") || return 0
    _destino=/storage/$_nome
    mkdir -p "$_destino" || return 0
    if mount -o rw "$_part" "$_destino" 2>>"$LOG"; then
        _log "montado $_part ($(_fstype "$_part")) em $_destino"
    else
        _log "FALHA ao montar $_part em $_destino"
        return 0
    fi
    # Liga no navegador do RetroArch (rgui_browser_directory = /boot/roms).
    # /boot e FAT: nao aceita symlink, entao e montagem mesmo (mount --bind).
    if mkdir -p "/boot/roms/$_nome" 2>/dev/null &&
       mount --bind "$_destino" "/boot/roms/$_nome" 2>>"$LOG"; then
        _log "$_destino ligado em /boot/roms/$_nome"
    fi
}

_desmontar() {
    _nome=$(_nome_do_slot "$1")
    _destino=/storage/$_nome
    if grep -q " $_destino " /proc/mounts; then
        umount "/boot/roms/$_nome" 2>/dev/null
        umount "$_destino" 2>/dev/null || umount -l "$_destino" 2>/dev/null
        _log "desmontado $_destino"
    fi
    rmdir "/boot/roms/$_nome" 2>/dev/null
}

_rescan() {
    _dev=/sys/bus/platform/drivers/$DRIVER
    [ -w "$_dev/bind" ] || return 0
    # Se o host estiver ligado, desliga antes: religar sem desligar nao forca
    # nova deteccao do cartao. Se ja estiver desligado (unbind falharia), vai
    # direto para o bind — e o que recupera o slot de um unbind manual/erro.
    if [ -d "$_dev/$1" ]; then
        echo "$1" > "$_dev/unbind" 2>/dev/null
        sleep 1
    fi
    echo "$1" > "$_dev/bind" 2>/dev/null || return 0
    _log "rescan do slot $1 (cartao inserido depois do boot)"
}

_log "iniciando (hotplug=$HOTPLUG, intervalo=${INTERVALO}s)"

# cooldown por slot, em epoch (variavel por slot: _cd_fe2b0000 / _cd_fe000000)
_cd_fe2b0000=0
_cd_fe000000=0

while :; do
    _roothost=$(_host_of_disk "$(_root_disk)")

    for _host in $SLOTS; do
        # Nunca religa o host do sistema: o root esta nele.
        if [ -n "${_roothost:-}" ] && [ "$_host" = "$_roothost" ]; then
            continue
        fi

        _disco=$(_disk_of_host "$_host")
        if [ -z "$_disco" ]; then
            _var="_cd_${_host%%.*}"
            eval "_ultimo=\${$_var:-0}"
            _agora=$(date +%s)
            if [ "$HOTPLUG" = on ] && [ $((_agora - _ultimo)) -ge "$COOLDOWN" ]; then
                eval "$_var=$_agora"
                _rescan "$_host"
            fi
            continue
        fi

        [ -b "/dev/$_disco" ] && _montar "$_host" "$_disco"
    done

    # Limpa montagem cujo dispositivo desapareceu (cartao removido).
    for _nome in sdcard1 sdcard2; do
        _destino=/storage/$_nome
        grep -q " $_destino " /proc/mounts || continue
        _src=$(awk -v d="$_destino" '$2 == d { print $1 }' /proc/mounts)
        [ -b "$_src" ] || _desmontar "$( [ "$_nome" = sdcard1 ] && echo fe2b0000.mmc || echo fe000000.mmc )"
    done

    sleep "$INTERVALO"
done
