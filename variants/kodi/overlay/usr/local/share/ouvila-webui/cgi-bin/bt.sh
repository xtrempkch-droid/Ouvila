#!/bin/sh
# CGI: Bluetooth (BlueZ via bluetoothctl). Saida em JSON.
#
# Acoes: status | scan | pair | remove | power
# Parametros: action, mac, on (1/0)
set -u

json_escape() {
    tr -d '\r' | awk 'BEGIN{RS="\n"} {gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); gsub(/\t/,"\\t"); if (NR>1) printf "\\n"; printf "%s", $0}'
}

url_decode() {
    printf '%s' "$1" | awk '
      {
        out=""; s=$0
        for (i=1;i<=length(s);i++) {
          c=substr(s,i,1)
          if (c=="+") out=out" "
          else if (c=="%" && i+2<=length(s)) {
            h=substr(s,i+1,2); i+=2
            v=0; for (j=1;j<=2;j++) { d=index("0123456789abcdef",tolower(substr(h,j,1))); if (d==0) {v=-1;break} v=v*16+d-1 }
            out=out (v>=0 ? sprintf("%c",v) : "%"h)
          } else out=out c
        }
        printf "%s", out
      }'
}

query_param() {
    name=$1
    for src in "$QUERY_STRING" "$POST_BODY"; do
        [ -n "$src" ] || continue
        old_ifs=$IFS; IFS='&'
        for pair in $src; do
            k=${pair%%=*}; v=${pair#*=}
            if [ "$k" = "$name" ]; then
                IFS=$old_ifs; url_decode "$v"; return 0
            fi
        done
        IFS=$old_ifs
    done
    return 1
}

POST_BODY=
len=${CONTENT_LENGTH:-0}
if [ "$len" -gt 0 ] 2>/dev/null; then POST_BODY=$(dd bs=1 count="$len" 2>/dev/null); fi

ACTION=$(query_param action || echo status)
MAC=$(query_param mac || true)
ON=$(query_param on || true)

printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'

# O bluetoothctl precisa do barramento do sistema; garante o daemon em pe.
if ! timeout 5 bluetoothctl show >/dev/null 2>&1; then
    systemctl start bluetooth >/dev/null 2>&1 || true
    sleep 2
fi

case "$ACTION" in
    status)
        info=$(timeout 5 bluetoothctl show 2>/dev/null)
        powered=$(printf '%s\n' "$info" | sed -n 's/.*Powered: //p' | head -n1)
        name=$(printf '%s\n' "$info" | sed -n 's/.*Name: //p' | head -n1)
        if [ "$powered" = "yes" ]; then pwr=true; else pwr=false; fi
        printf '{"powered":%s,"name":"%s","devices":[' "$pwr" "$(printf '%s' "$name" | json_escape)"
        first=1
        timeout 5 bluetoothctl devices 2>/dev/null | while read -r _ mac rest; do
            # So aceita MAC de verdade; mensagens como "No default controller
            # available" tambem saem nesse stream e nao podem virar "dispositivo".
            case "$mac" in
                [0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:*) : ;;
                *) continue ;;
            esac
            [ $first -eq 1 ] || printf ','
            first=0
            printf '{"mac":"%s","name":"%s"}' "$mac" "$(printf '%s' "$rest" | json_escape)"
        done
        printf ']}'
        ;;
    scan)
        timeout 12 bluetoothctl --timeout 8 scan on >/dev/null 2>&1 || true
        printf '['
        first=1
        timeout 5 bluetoothctl devices 2>/dev/null | while read -r _ mac rest; do
            # So aceita MAC de verdade; mensagens como "No default controller
            # available" tambem saem nesse stream e nao podem virar "dispositivo".
            case "$mac" in
                [0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:*) : ;;
                *) continue ;;
            esac
            [ $first -eq 1 ] || printf ','
            first=0
            printf '{"mac":"%s","name":"%s"}' "$mac" "$(printf '%s' "$rest" | json_escape)"
        done
        printf ']'
        ;;
    power)
        if [ "$ON" = "1" ]; then out=$(bluetoothctl power on 2>&1); else out=$(bluetoothctl power off 2>&1); fi
        printf '{"ok":true,"message":"%s"}' "$(printf '%s' "$out" | json_escape)"
        ;;
    pair)
        case "$MAC" in
            [0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:*) : ;;
            *) printf '{"ok":false,"error":"mac invalido"}'; exit 0 ;;
        esac
        out=$(bluetoothctl pair "$MAC" 2>&1; bluetoothctl trust "$MAC" 2>&1; bluetoothctl connect "$MAC" 2>&1)
        printf '{"ok":true,"message":"%s"}' "$(printf '%s' "$out" | json_escape)"
        ;;
    remove)
        out=$(bluetoothctl remove "$MAC" 2>&1)
        printf '{"ok":true,"message":"%s"}' "$(printf '%s' "$out" | json_escape)"
        ;;
    *)
        printf '{"ok":false,"error":"acao desconhecida"}'
        ;;
esac
