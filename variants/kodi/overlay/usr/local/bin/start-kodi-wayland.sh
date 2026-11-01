#!/bin/sh
# Sessão gráfica do Kodi no tty1.
#
# Tenta primeiro Weston (backend DRM, shell kiosk) + Kodi em Wayland, que é o
# caminho que aplica a rotação (display_transform). Se o Weston não subir,
# cai para GBM direto (sem compositor). O motivo de qualquer falha aparece NA
# TELA (tty1) e no log /boot/kodi-start.log, para não depender de SSH.
set -eu

export HOME=/root
: "${XDG_RUNTIME_DIR:=/run/kodi-wayland}"
export XDG_RUNTIME_DIR
export WAYLAND_DISPLAY=wayland-0

KODI_BIN=${KODI_BIN:-/usr/bin/kodi}
WESTON_LOG=${WESTON_LOG:-/var/log/weston.log}
OUVILA_CONF=${OUVILA_CONF:-/boot/ouvila.conf}
KODI_LOG=${KODI_LOG:-/boot/kodi-start.log}

say_tty() {
    { printf '\033[2J\033[H'; printf '%s\n' "$*"; } > /dev/tty1 2>/dev/null || true
    printf '%s\n' "$*" >&2
}

# Rotação padrão por modelo, lida do device tree em uso. A X55 tem painel
# 720x1280 montado deitado (precisa de rotate-90); X35S/X35H tem painel
# 640x480 e ficam em "normal". Assim uma única imagem serve nos três sem
# editar nada à mão.
transform_do_modelo() {
    modelo=
    if [ -r /proc/device-tree/model ]; then
        modelo=$(tr -d '\000' < /proc/device-tree/model 2>/dev/null | tr 'A-Z' 'a-z')
    fi
    case "$modelo" in
        *x55*) printf 'rotate-90\n' ;;
        *)     printf 'normal\n' ;;
    esac
}

# Rotação da saída DSI-1 (valores do Weston). Vem de "display_transform=<valor>"
# em ouvila.conf, na partição FAT (BOOT), editável no PC sem recompilar.
# "auto" (ou vazio/valor inválido) usa o padrão do modelo.
read_transform() {
    value=
    if [ -r "$OUVILA_CONF" ]; then
        value=$(sed -n 's/^[[:space:]]*display_transform[[:space:]]*=[[:space:]]*//p' "$OUVILA_CONF" \
            | tail -n 1 | tr -d '\r[:space:]')
    fi
    case "$value" in
        normal|rotate-90|rotate-180|rotate-270|\
        flipped|flipped-rotate-90|flipped-rotate-180|flipped-rotate-270)
            printf '%s\n' "$value"
            ;;
        *)
            transform_do_modelo
            ;;
    esac
}

TRANSFORM=$(read_transform)

KODI_LOG_DIR=$(dirname "$KODI_LOG")
if [ ! -d "$KODI_LOG_DIR" ] || [ ! -w "$KODI_LOG_DIR" ]; then
    KODI_LOG=/var/log/kodi-start.log
fi

echo "===== $(date -Is) start-kodi-wayland (transform=$TRANSFORM) =====" >> "$KODI_LOG"

weston_pid=
stop_weston() {
    if [ -n "$weston_pid" ]; then
        kill "$weston_pid" 2>/dev/null || true
        wait "$weston_pid" 2>/dev/null || true
        weston_pid=
    fi
}
trap 'stop_weston' EXIT INT TERM

start_weston() {
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
    rm -f "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY.lock"

    WESTON_INI="$XDG_RUNTIME_DIR/weston.ini"
    cat > "$WESTON_INI" << EOF_INI
[core]
shell=kiosk-shell.so
idle-time=0

[output]
name=DSI-1
transform=$TRANSFORM
EOF_INI

    # --socket explícito: o Weston 10 cria "wayland-1" quando WAYLAND_DISPLAY
    # está no ambiente; sem isso, o laço abaixo espera "wayland-0" para sempre
    # (Weston estava funcionando e era morto pelo timeout).
    weston --backend=drm-backend.so --tty=1 --socket=wayland-0 --idle-time=0 \
        --config="$WESTON_INI" --log="$WESTON_LOG" >> "$KODI_LOG" 2>&1 &
    weston_pid=$!

    attempt=0
    while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
        if ! kill -0 "$weston_pid" 2>/dev/null; then
            echo "Weston saiu antes de criar o socket Wayland." >> "$KODI_LOG"
            [ -r "$WESTON_LOG" ] && cat "$WESTON_LOG" >> "$KODI_LOG" 2>/dev/null || true
            weston_pid=
            return 1
        fi
        if [ "$attempt" -ge 30 ]; then
            echo "Timeout esperando o socket do Weston." >> "$KODI_LOG"
            stop_weston
            return 1
        fi
        attempt=$((attempt + 1))
        sleep 1
    done
    return 0
}

ok=0

# Tentativa 1: Weston (Wayland).
if start_weston; then
    echo "Weston pronto; iniciando Kodi --windowing=wayland" >> "$KODI_LOG"
    set +e
    "$KODI_BIN" --standalone --windowing=wayland >> "$KODI_LOG" 2>&1
    kodi_status=$?
    set -e
    echo "Kodi (wayland) terminou com status $kodi_status" >> "$KODI_LOG"
    [ "$kodi_status" -eq 0 ] && ok=1
else
    say_tty "KODI: Weston falhou. Veja /boot/kodi-start.log. Tentando GBM direto..."
fi
stop_weston

# Tentativa 2: GBM direto (sem compositor). O painel já traz rotation=270 no
# device tree, então o Kodi deve abrir sem transform do Weston. Dá um tempo
# para o DRM ser liberado pelo Weston antes de o Kodi tentar pegá-lo.
if [ "$ok" -eq 0 ]; then
    sleep 3
    say_tty "KODI: tentando --windowing=gbm (sem Weston)..."
    set +e
    "$KODI_BIN" --standalone --windowing=gbm >> "$KODI_LOG" 2>&1
    kodi_status=$?
    set -e
    echo "Kodi (gbm) terminou com status $kodi_status" >> "$KODI_LOG"
    [ "$kodi_status" -eq 0 ] && ok=1
fi

if [ "$ok" -eq 0 ]; then
    say_tty "KODI NAO ABRIU (veja /boot/kodi-start.log)"
    exit 1
fi

exit 0
