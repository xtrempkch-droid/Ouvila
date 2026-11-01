#!/bin/sh
# Diagnóstico de boot. Faz DUAS capturas (15s e 60s) porque a falha do Kodi/USB
# costuma estar em ciclo (kodi.service reinicia) e o estado no instante importa.
OUT=/boot/diagnostico.txt
: > "$OUT"

capture() {
  {
    echo "===== $(date -Is) — captura $1 ====="
  echo
  echo "----- /proc/cmdline (imagem e parametros de boot) -----"
  cat /proc/cmdline
  echo
  echo "----- nós de entrada do device tree em uso -----"
  for node in rocknix-joypad gpio-keys-gamepad adc-joystick; do
    path="/proc/device-tree/$node"
    if [ -d "$path" ]; then
      printf '%s: ' "$node"
      tr '\000' ' ' < "$path/compatible"
      echo
    fi
  done
  echo
  if [ "$1" = "15s" ]; then
    echo "----- dmesg (log do kernel) -----"
    dmesg
    echo
    echo "----- /proc/bus/input/devices (botoes/analogico) -----"
    cat /proc/bus/input/devices
    echo
    echo "----- lsmod (modulos carregados) -----"
    lsmod
    echo
  fi
  echo "----- /sys/class/udc (controlador USB em modo gadget) -----"
  ls -la /sys/class/udc/ 2>&1
  echo
  echo "----- modelo do aparelho (device tree em uso) -----"
  tr -d '\000' < /proc/device-tree/model 2>&1 || echo "(sem /proc/device-tree/model)"
  echo
  echo "----- USB: dr_mode do nó usb@fcc00000 no device tree em uso -----"
  cat /proc/device-tree/usb@fcc00000/dr_mode 2>&1 || echo "(nó/atributo ausente)"
  echo
  echo "----- tela: modo e taxa ativos (responde 'o 75 Hz aplicou?') -----"
  for m in /sys/class/drm/card0-*/modes; do
    [ -e "$m" ] || continue
    printf '%s (primeiro = modo preferido): ' "$m"
    head -n 3 "$m" | tr '\n' ' '
    echo
  done
  echo "conector/status:"
  for st in /sys/class/drm/card0-*/status; do
    [ -e "$st" ] || continue
    printf '%s: ' "$st"; cat "$st" 2>&1
  done
  echo
  echo "----- rotação efetiva (ouvila.conf + weston.ini) -----"
  tr -d '\r' < /boot/ouvila.conf 2>&1 | grep -i transform || echo "(sem display_transform)"
  grep -i '^transform' /run/kodi-wayland/weston.ini 2>&1 || echo "(weston.ini sem transform)"
  echo
  echo "----- USB: estado dos UDC (gadget) -----"
  for f in /sys/class/udc/*/state; do
    printf '%s: ' "$f"; cat "$f" 2>&1
  done
  echo
  echo "----- USB: papéis (usb_role) -----"
  ls -la /sys/class/usb_role/ 2>&1
  for f in /sys/class/usb_role/*/role; do
    printf '%s: ' "$f"; cat "$f" 2>&1
  done
  echo
  echo "----- USB: dmesg filtrado (dwc3 / ep0 / phy / g_ether) -----"
  dmesg | grep -iE 'dwc3|ep0|usb2phy|g_ether|udc|drd' || echo "(nada relacionado)"
  echo
  echo "----- ip addr / link (interfaces de rede) -----"
  ip addr
  echo
  ip -br link
  echo
  echo "----- USB: modulos de gadget carregados -----"
  lsmod | grep -E 'g_ether|u_ether|usb_f_|libcomposite' || echo "(nenhum modulo de gadget carregado)"
  echo
  echo "----- USB: configfs de gadget -----"
  ls -la /sys/kernel/config/usb_gadget/ 2>&1
  echo
  echo "----- systemctl status kodi -----"
  systemctl status kodi.service --no-pager -l
  echo
  echo "----- systemctl status usb-gadget -----"
  systemctl status usb-gadget.service --no-pager -l
  echo
  echo "----- journalctl -b -u kodi -u usb-gadget (ultimas 150 linhas) -----"
  journalctl -b -u kodi -u usb-gadget -n 150 --no-pager
  echo
  echo "----- /boot/kodi-start.log (saida do Kodi; motivo da saida) -----"
  cat /boot/kodi-start.log 2>&1 || echo "(arquivo nao existe: o Kodi nao chegou a rodar?)"
  echo
  echo "----- /boot/retroarch-start.log (variante retroarch) -----"
  cat /boot/retroarch-start.log 2>&1 || echo "(nao existe: imagem kodi ou o RetroArch nao rodou)"
  echo
  echo "----- RetroArch: configuracao efetiva (variante retroarch) -----"
  for cfg in /root/.config/retroarch/retroarch.cfg; do
    [ -r "$cfg" ] || continue
    printf '%s:\n' "$cfg"
    grep -E '^(video_driver|video_context_driver|video_fullscreen|audio_driver|libretro_directory|libretro_info_path|menu_driver)' "$cfg"
  done
  echo
  # Sem perfil de autoconfig NENHUM botao responde no RetroArch (o pacote Debian
  # nao traz nenhum). Aqui o diagnostico mostra se o perfil esta la e qual bitmap
  # de botoes o kernel entrega — e dele que saem os indices do autoconfig.
  echo "----- RetroArch: perfil de controle (autoconfig) -----"
  if [ -d /root/.config/retroarch/autoconfig ]; then
    find /root/.config/retroarch/autoconfig -name '*.cfg' 2>/dev/null | while read -r f; do
      printf '%s: ' "$f"
      grep -q 'input_device' "$f" && grep 'input_device' "$f" || echo "(sem input_device)"
    done
  else
    echo "AUSENTE: sem autoconfig o joypad nao responde (log: 'not configured')"
  fi
  echo
  echo "----- entrada: nome e capabilities do joypad (vide docs/hardware-e-drivers.md §3.2.1) -----"
  for ev in /sys/class/input/event*/device/capabilities/key; do
    [ -r "$ev" ] || continue
    dev=$(dirname "$(dirname "$ev")")          # .../input/eventN/device
    nodo=$(basename "$(dirname "$dev")")       # eventN
    printf '%s -> %s | key=%s' "$nodo" \
      "$(cat "$dev/name" 2>/dev/null)" "$(cat "$ev" 2>/dev/null)"
    [ -r "$dev/capabilities/abs" ] && printf ' | abs=%s' "$(cat "$dev/capabilities/abs" 2>/dev/null)"
    echo
  done
  echo
  echo "----- RetroArch: cores instalados -----"
  for d in /usr/lib/*/libretro /usr/lib/libretro; do
    [ -d "$d" ] || continue
    # No Debian os cores sao *.libretro (nao *.so); contar as duas extensoes.
    printf '%s: %s core(s)\n' "$d" "$(ls -1 "$d"/*.libretro "$d"/*.so 2>/dev/null | wc -l)"
    ls -1 "$d"/*.libretro "$d"/*.so 2>/dev/null | xargs -r -n1 basename | tr '\n' ' '
    echo
  done
  echo
  echo "----- /boot/usb-watch.log (estado do USB a cada mudanca) -----"
  cat /boot/usb-watch.log 2>&1 || echo "(nao existe)"
  echo
  echo "----- /boot/weston-falhou.log (se o Weston morreu) -----"
  cat /boot/weston-falhou.log 2>&1 || echo "(nao existe)"
  echo "----- /var/log/weston.log (procure Invalid transform) -----"
  cat /var/log/weston.log 2>&1 || echo "(arquivo não existe ainda)"
  echo
  echo "----- /boot/ouvila.conf -----"
  cat /boot/ouvila.conf 2>&1 || echo "(arquivo não existe)"
  echo
  echo "----- weston.ini efetivo (gerado no boot) -----"
  cat /run/kodi-wayland/weston.ini 2>&1 || echo "(arquivo não existe)"
  echo
  echo "----- dispositivos DRM -----"
  ls -la /dev/dri/ 2>&1
  echo
  echo "----- /root/.kodi/temp/kodi.log (log interno do Kodi) -----"
  tail -n 200 /root/.kodi/temp/kodi.log 2>&1 || echo "(arquivo não existe ainda)"
  } >> "$OUT" 2>&1
}

sleep 15
capture "15s"
sleep 45
capture "60s"
