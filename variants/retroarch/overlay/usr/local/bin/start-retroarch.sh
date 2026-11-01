#!/bin/sh
# Sessão gráfica do RetroArch no tty1 (variante "retroarch" = versão de jogos).
#
# Mesmo desenho do launcher do Kodi: Weston (backend DRM, kiosk) aplica a rotação
# e o RetroArch roda em Wayland por cima. Se o Weston não subir, tenta o driver
# KMS/DRM do próprio RetroArch (sem compositor). O motivo de qualquer falha
# aparece NA TELA (tty1) e em /boot/retroarch-start.log, sem precisar de SSH.
set -eu

export HOME=/root

RETROARCH_BIN=${RETROARCH_BIN:-/usr/bin/retroarch}
RETROARCH_CFG=${RETROARCH_CFG:-/root/.config/retroarch/retroarch.cfg}
RETROARCH_LOG=${RETROARCH_LOG:-/boot/retroarch-start.log}

# Rotação e start do Weston vêm do helper compartilhado (fonte canônica).
# A variável permite testar o launcher sem instalar nada no sistema.
OUVILA_DISPLAY_LIB=${OUVILA_DISPLAY_LIB:-/usr/local/share/ouvila/display.sh}
. "$OUVILA_DISPLAY_LIB"

TRANSFORM=$(ouvila_read_transform)

# A rotação é aplicada pelo Weston (transform= na saída), mas o RetroArch em
# Wayland dimensiona a PRÓPRIA superfície com video_fullscreen_x/y. Em fullscreen
# com esses valores em 0 ele usa o modo NATIVO do painel (720x1280 na X55) e pede
# uma superfície maior do que o fullscreen girado (1280x720), o que é violação de
# protocolo: o Weston derruba o cliente e a tela fica preta.
#   xdg_surface geometry (720 x 1280) is larger than the configured
#   fullscreen state (1280 x 720)
# Por isso o tamanho da saída JÁ GIRADA é calculado e passado por --appendconfig.
SIZE=$(ouvila_rotated_size "$TRANSFORM")
SIZE_W=${SIZE%% *}
SIZE_H=${SIZE##* }

# Sessão Wayland mais curta que isto não é "o usuário saiu do menu": é morte
# precoce (protocolo, driver, ...). Serve para não declarar sucesso com tela preta.
MIN_SEGUNDOS=${OUVILA_MIN_SEGUNDOS:-10}

# O que fazer quando o usuário fecha o RetroArch pelo menu.
#
# O RetroArch 1.14 NÃO tem item "Desligar" no menu (só "Restart RetroArch" e
# "Quit RetroArch"), e "Quit" apenas encerra o processo: o aparelho continuaria
# ligado, sem interface no tty1 (o getty é mascarado) e sem como desligar sem
# SSH/serial. Por isso a saída normal DESLIGA o aparelho, como nas imagens
# ROCKNIX. Ajustável em /boot/ouvila.conf (editável no PC):
#   retroarch_exit=poweroff (padrão) -> systemctl poweroff
#   retroarch_exit=stay              -> só sai (depurar por SSH/serial)
EXIT_ACTION=$(ouvila_read_conf_key retroarch_exit) || EXIT_ACTION=
case "$EXIT_ACTION" in
    stay|none|shell) ;;
    *)               EXIT_ACTION=poweroff ;;
esac

_log_dir=$(dirname "$RETROARCH_LOG")
if [ ! -d "$_log_dir" ] || [ ! -w "$_log_dir" ]; then
    RETROARCH_LOG=/var/log/retroarch-start.log
fi

echo "===== $(date -Is) start-retroarch (transform=$TRANSFORM) =====" >> "$RETROARCH_LOG"

if [ ! -x "$RETROARCH_BIN" ]; then
    ouvila_say_tty "RETROARCH: binário não encontrado em $RETROARCH_BIN"
    echo "binário ausente: $RETROARCH_BIN" >> "$RETROARCH_LOG"
    exit 1
fi

# --config explícito: o serviço roda como root com HOME=/root, e o padrão do
# RetroArch é ~/.config/retroarch/retroarch.cfg. Ser explícito evita "abriu com
# a configuração errada" quando o HOME muda.
set -- --config "$RETROARCH_CFG"
if [ -n "${OUVILA_ROM:-}" ]; then
    set -- "$@" "$OUVILA_ROM"
fi

# Escreve o arquivo de override e imprime o caminho. $1 = "wayland" ou "drm".
# --appendconfig é aplicado DEPOIS do --config, então vence o retroarch.cfg.
_escrever_override() {
    : "${XDG_RUNTIME_DIR:=/run/ouvila-wayland}"
    _arquivo="$XDG_RUNTIME_DIR/retroarch-override.cfg"
    mkdir -p "$XDG_RUNTIME_DIR"

    if [ "$1" = drm ]; then
        # Sem compositor a rotação não é aplicada por ninguém (o driver "drm"
        # do 1.14 não implementa set_rotation), então o tamanho correto é o
        # nativo do painel — na X55 a imagem sai deitada, mas legível.
        _tam=$(ouvila_native_size)
    else
        _tam="$SIZE"
    fi

    _l=${_tam%% *}
    _a=${_tam##* }

    : > "$_arquivo"
    if [ "$_l" -gt 0 ] 2>/dev/null && [ "$_a" -gt 0 ] 2>/dev/null; then
        printf 'video_fullscreen_x = "%s"\nvideo_fullscreen_y = "%s"\n' "$_l" "$_a" >> "$_arquivo"
    fi
    if [ "$1" = drm ]; then
        # Identificadores no 1.14: driver "drm" (no 1.17+ virou "kmsdrm") e
        # contexto "kms". O 1.14 NÃO tem --video-driver na linha de comando.
        printf 'video_driver = "drm"\nvideo_context_driver = "kms"\n' >> "$_arquivo"
    fi
    printf '%s\n' "$_arquivo"
}

ok=0

# Tentativa 1: Weston (Wayland) — é o caminho que aplica a rotação.
_inicio=$(date +%s)
if ouvila_start_weston "$TRANSFORM" "$RETROARCH_LOG"; then
    echo "Weston pronto; iniciando RetroArch (wayland, tela ${SIZE_W}x${SIZE_H})" >> "$RETROARCH_LOG"
    _override=$(_escrever_override wayland)
    set +e
    "$RETROARCH_BIN" "$@" --appendconfig "$_override" >> "$RETROARCH_LOG" 2>&1
    status=$?
    set -e
    _durou=$(( $(date +%s) - _inicio ))
    echo "RetroArch (wayland) terminou com status $status apos ${_durou}s" >> "$RETROARCH_LOG"
    if [ "$status" -eq 0 ] && [ "$_durou" -ge "$MIN_SEGUNDOS" ]; then
        ok=1
    elif [ "$status" -eq 0 ]; then
        echo "sessao curta demais (${_durou}s): tratando como falha, nao como saida do usuario" >> "$RETROARCH_LOG"
    fi
else
    ouvila_say_tty "RETROARCH: Weston falhou. Veja /boot/retroarch-start.log. Tentando KMS/DRM direto..."
fi
ouvila_stop_weston

# Tentativa 2: KMS/DRM direto, sem compositor. Dá um tempo para o DRM ser
# liberado pelo Weston antes de o RetroArch tentar pegá-lo; sem isso ele
# reclama que o dispositivo está ocupado.
if [ "$ok" -eq 0 ]; then
    sleep 3
    ouvila_say_tty "RETROARCH: tentando video_driver=drm (sem Weston)..."
    _cfg_extra=$(_escrever_override drm)
    _inicio=$(date +%s)
    set +e
    "$RETROARCH_BIN" "$@" --appendconfig "$_cfg_extra" >> "$RETROARCH_LOG" 2>&1
    status=$?
    set -e
    _durou=$(( $(date +%s) - _inicio ))
    echo "RetroArch (drm) terminou com status $status apos ${_durou}s" >> "$RETROARCH_LOG"
    # Mesmo critério da tentativa 1: sair cedo com status 0 não é sucesso.
    if [ "$status" -eq 0 ] && [ "$_durou" -ge "$MIN_SEGUNDOS" ]; then
        ok=1
    fi
fi

# Sair do RetroArch pelo menu é uma saída normal (status 0). Se nada rodou,
# explica na tela em vez de deixar o usuário numa tela preta sem motivo.
if [ "$ok" -eq 0 ]; then
    ouvila_say_tty "RETROARCH NAO ABRIU (veja /boot/retroarch-start.log)"
    exit 1
fi

# Saída normal do usuário: desliga o aparelho (o RetroArch não tem opção de
# shutdown no menu). "sync" antes de desligar porque a partição FAT (ROMs) e o
# ext4 (saves) podem ter escrita pendente — o RetroArch salva o retroarch.cfg no
# exit e o save do jogo pode ter acabado de ser gravado.
# --no-block: o systemd vai parar este serviço para desligar, então não faz
# sentido esperar (e sim morrer no meio do desligamento).
if [ "$EXIT_ACTION" = poweroff ]; then
    echo "Saida normal do usuario (status 0 apos ${_durou:-?}s): desligando o aparelho" >> "$RETROARCH_LOG"
    ouvila_say_tty "Desligando..."
    sync
    if command -v systemctl >/dev/null 2>&1; then
        systemctl poweroff --no-block >> "$RETROARCH_LOG" 2>&1 || true
    else
        poweroff >> "$RETROARCH_LOG" 2>&1 || true
    fi
fi

exit 0
