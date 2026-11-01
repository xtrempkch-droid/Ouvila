#!/bin/sh
# Launcher da variante "media-player" no tty1.
#
# A UI é o próprio binário walkmam-media (DRM/KMS + FFmpeg + ALSA). Não há
# compositor: este script só prepara o ambiente, registra o log no cartão e
# trata a saída.
#
# Regras (herdadas do que já foi validado nas outras variantes):
#   * nada de Restart=always: uma falha não pode virar laço que apaga a
#     mensagem de erro da tela;
#   * o motivo de qualquer falha vai para /boot/media-player.log e para o tty1;
#   * sessão curta com status 0 NÃO é "o usuário saiu": conta como falha.
#
# Configuração em /boot/walkmam.conf (partição FAT, editável no PC):
#   media_exit=poweroff | stay   (padrão poweroff, comportamento de console)
#   display_transform=auto | normal | rotate-90 | ...
#   media_volume=0..100
#   media_audio_out=default | hw:0,0 | -
set -eu

export HOME=/root

BIN=${MEDIA_BIN:-/usr/local/bin/walkmam-media}
MEDIA_LOG=${MEDIA_LOG:-/boot/media-player.log}
MIN_SEGUNDOS=${WALKMAM_MIN_SEGUNDOS:-10}

# Helper compartilhado: leitura do walkmam.conf e escrita no tty1.
WALKMAM_DISPLAY_LIB=${WALKMAM_DISPLAY_LIB:-/usr/local/share/walkmam/display.sh}
if [ -r "$WALKMAM_DISPLAY_LIB" ]; then
    . "$WALKMAM_DISPLAY_LIB"
else
    # A variante não depende do helper para funcionar (ele é só conveniência).
    walkmam_say_tty() { { printf '\033[2J\033[H'; printf '%s\n' "$*"; } > /dev/tty1 2>/dev/null || true; }
    walkmam_read_conf_key() {
        [ -r /boot/walkmam.conf ] || return 0
        sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" /boot/walkmam.conf \
            | tail -n 1 | tr -d '\r[:space:]'
    }
fi

EXIT_ACTION=$(walkmam_read_conf_key media_exit) || EXIT_ACTION=
case "$EXIT_ACTION" in
    stay|none|shell) ;;
    *)               EXIT_ACTION=poweroff ;;
esac

_log_dir=$(dirname "$MEDIA_LOG")
if [ ! -d "$_log_dir" ] || [ ! -w "$_log_dir" ]; then
    MEDIA_LOG=/var/log/media-player.log
fi

echo "===== $(date -Is) start-media-player =====" >> "$MEDIA_LOG"

if [ ! -x "$BIN" ]; then
    walkmam_say_tty "MEDIA-PLAYER: binário ausente em $BIN"
    echo "binario ausente: $BIN" >> "$MEDIA_LOG"
    exit 1
fi

# O app abre o DRM direto. Se sobrar um compositor ou getty no tty1, eles
# brigam pelo master do KMS — por isso o serviço já declara
# Conflicts=getty@tty1.service e o getty do tty1 é mascarado na imagem.
_inicio=$(date +%s)
set +e
"$BIN" --config /boot/walkmam.conf >> "$MEDIA_LOG" 2>&1
status=$?
set -e
_durou=$(( $(date +%s) - _inicio ))
echo "walkmam-media terminou com status $status apos ${_durou}s" >> "$MEDIA_LOG"

if [ "$status" -eq 0 ] && [ "$_durou" -ge "$MIN_SEGUNDOS" ]; then
    if [ "$EXIT_ACTION" = poweroff ]; then
        echo "saida normal (status 0): desligando" >> "$MEDIA_LOG"
        walkmam_say_tty "Desligando..."
        sync
        if command -v systemctl >/dev/null 2>&1; then
            systemctl poweroff --no-block >> "$MEDIA_LOG" 2>&1 || true
        else
            poweroff >> "$MEDIA_LOG" 2>&1 || true
        fi
    fi
    exit 0
fi

if [ "$status" -eq 0 ]; then
    echo "sessao curta demais (${_durou}s): tratando como falha" >> "$MEDIA_LOG"
fi

walkmam_say_tty "MEDIA-PLAYER NAO ABRIU (status $status). Veja /boot/media-player.log"
exit 1
