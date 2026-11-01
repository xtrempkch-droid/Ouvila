#!/bin/sh
# Grava uma imagem do Ouvila no eMMC do aparelho (mmc0 / fe310000.mmc).
#
# Por que um script: o eMMC destas placas tem 3776 MiB e a imagem tem 3584 MiB —
# cabe, mas so por ~190 MiB de margem. Um dd maior que o disco escreve ate encher
# e termina com "No space left on device" deixando o eMMC com instalacao
# truncada (que nao inicia). Aqui o tamanho e conferido ANTES de escrever.
#
# Tambem zera a tabela de particoes antiga antes: o ROCKNIX de fabrica usa GPT e
# a nossa imagem usa MBR. Sem zerar, a GPT de backup no fim do disco continua la
# e o kernel reclama ("GPT PMBR size mismatch") podendo "consertar" a tabela.
#
# Uso (no aparelho, com o sistema rodando do SD):
#   /usr/local/bin/install-image-to-emmc.sh /boot/rk3566-x55-retroarch-sdcard.img
#   /usr/local/bin/install-image-to-emmc.sh /boot/imagem.img.gz   (descompacta)
#
# NAO rode com pressa: isso apaga o eMMC. O SD com o sistema em uso nao e tocado.
set -eu

IMG=${1:-}
EMMC=${OUVILA_EMMC:-/dev/mmcblk0}

die() { echo "ERRO: $*" >&2; exit 1; }

[ -n "$IMG" ] || die "informe a imagem (.img ou .img.gz), ex.: /boot/rk3566-x55-mainline-sdcard.img"
[ -r "$IMG" ] || die "nao consigo ler $IMG"
[ "$(id -u)" -eq 0 ] || die "precisa ser root"

# 1) Confere que o alvo e mesmo o eMMC e nao o cartao do sistema.
# /dev/mmcblk0 e um no de dispositivo (devtmpfs), NAO um symlink: o caminho da
# controladora so existe no sysfs (/sys/class/block/<disco> -> .../fe310000.mmc).
_sys="/sys/class/block/$(basename "$EMMC")"
[ -e "$_sys" ] || die "$EMMC nao existe"
_real=$(readlink -f "$_sys")
case "$_real" in
    *fe310000.mmc*) ;;
    *) die "$EMMC nao parece ser o eMMC (esperado algo sob fe310000.mmc; o sysfs diz ${_real:-nada}). Use OUVILA_EMMC=... se souber o que esta fazendo." ;;
esac
_rootpart=$(awk '$2 == "/" { print $1; exit }' /proc/mounts)
case "$_rootpart" in
    "$EMMC"*) die "o sistema esta rodando DO eMMC; grave a partir de um SD" ;;
esac

_discos=$(lsblk -no NAME,TYPE 2>/dev/null | awk '$2=="disk"{print $1}')
printf '%s\n' "$_discos" | grep -qx "$(basename "$EMMC")" || die "$EMMC nao existe"

# 2) Descobre o tamanho da imagem (sem extrair ainda).
case "$IMG" in
    *.gz)
        _bytes=$(gzip -l "$IMG" | awk 'NR==2 {print $2}')
        ;;
    *)
        _bytes=$(stat -c %s "$IMG")
        ;;
esac
[ -n "${_bytes:-}" ] && [ "$_bytes" -gt 0 ] 2>/dev/null || die "nao consegui medir o tamanho de $IMG"

_sectors=$(cat "/sys/block/$(basename "$EMMC")/size")
_tam_disco_mb=$(( _sectors * 512 / 1024 / 1024 ))
_tam_img_mb=$(( _bytes / 1024 / 1024 ))
echo "eMMC ($EMMC): ${_tam_disco_mb} MiB"
echo "imagem ($IMG): ${_tam_img_mb} MiB"
if [ "$_tam_img_mb" -gt "$_tam_disco_mb" ]; then
    die "a imagem NAO cabe no eMMC (${_tam_img_mb} MiB > ${_tam_disco_mb} MiB). Gere com OUVILA_IMAGE_SIZE_MB menor."
fi

# 3) Confirmacao explicita: a operacao e destrutiva.
echo
echo ">>> TUDO no $EMMC sera APAGADO (inclui o ROCKNIX de fabrica, se existir)."
printf 'Digite APAGAR para continuar: '
read -r _resposta || die "cancelado (sem entrada para confirmar)"
[ "$_resposta" = APAGAR ] || die "cancelado"

# 4) Desmonta tudo que estiver em cima do eMMC.
for _p in "$EMMC"p*; do
    [ -b "$_p" ] || continue
    if grep -q "^$_p " /proc/mounts; then
        echo "--> desmontando $_p"
        umount "$_p" || die "nao consegui desmontar $_p"
    fi
done

# 5) Zera as tabelas antigas (GPT de backup inclusive) antes de escrever.
echo "--> limpando tabelas de particao antigas"
command -v wipefs >/dev/null 2>&1 && wipefs -a "$EMMC" >/dev/null 2>&1 || true
command -v sgdisk >/dev/null 2>&1 && sgdisk --zap-all "$EMMC" >/dev/null 2>&1 || true
dd if=/dev/zero of="$EMMC" bs=1M count=8 conv=fsync status=none
# ultimos 8 MiB (GPT de backup no fim do disco)
_seek=$(( _tam_disco_mb - 8 ))
[ "$_seek" -gt 0 ] && dd if=/dev/zero of="$EMMC" bs=1M seek="$_seek" count=8 conv=fsync status=none

# 6) Escreve a imagem.
echo "--> gravando (isso leva alguns minutos)"
if [ "${IMG##*.}" = gz ]; then
    gzip -dc "$IMG" | dd of="$EMMC" bs=4M conv=fsync status=progress
else
    dd if="$IMG" of="$EMMC" bs=4M conv=fsync status=progress
fi
sync

echo
echo "--> pronto. Conferindo o que ficou no eMMC:"
# partprobe vem do parted, que pode nao estar no rootfs: o '|| true' evita
# transformar um aviso cosmetico em falha depois de ja ter gravado.
command -v partprobe >/dev/null 2>&1 && partprobe "$EMMC" 2>/dev/null || true
sleep 1
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTUUID "$EMMC" 2>/dev/null || true
echo
echo "Proximo passo: desligar, REMOVER o cartao SD e ligar."
echo "O U-Boot procura sistema em SD1, depois no eMMC (BOOT_TARGETS)."
