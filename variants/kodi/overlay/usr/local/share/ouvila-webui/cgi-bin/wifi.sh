#!/bin/sh
# CGI: Wi-Fi (NetworkManager). Saida em JSON.
#
# Acoes: status | scan | connect | disconnect | forget
# Parametros (query string ou corpo POST urlencoded): action, ssid, password
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
                IFS=$old_ifs
                url_decode "$v"
                return 0
            fi
        done
        IFS=$old_ifs
    done
    return 1
}

POST_BODY=
len=${CONTENT_LENGTH:-0}
if [ "$len" -gt 0 ] 2>/dev/null; then
    POST_BODY=$(dd bs=1 count="$len" 2>/dev/null)
fi

ACTION=$(query_param action || echo status)
SSID=$(query_param ssid || true)
PASSWORD=$(query_param password || true)

printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'

case "$ACTION" in
    status)
        line=$(nmcli -t -f DEVICE,STATE,CONNECTION dev status 2>/dev/null | awk -F: '$1=="wlan0"{print}' | head -n1)
        # nmcli -t: wlan0:<STATE>:<CONNECTION>
        dev_state=$(printf '%s' "$line" | cut -d: -f2)
        conn=$(printf '%s' "$line" | cut -d: -f3)
        ip=$(nmcli -t -f IP4.ADDRESS dev show wlan0 2>/dev/null | awk -F: '{print $2}' | head -n1)
        printf '{"device":"wlan0","state":"%s","connection":"%s","ip":"%s","radio":"%s"}' \
            "$(printf '%s' "$dev_state" | json_escape)" \
            "$(printf '%s' "$conn" | json_escape)" \
            "$(printf '%s' "$ip" | json_escape)" \
            "$(nmcli radio wifi 2>/dev/null | json_escape)"
        ;;
    scan)
        nmcli dev wifi rescan >/dev/null 2>&1 || true
        sleep 4
        printf '['
        first=1
        nmcli -t -f SSID,SIGNAL,SECURITY,IN-USE dev wifi list 2>/dev/null \
          | awk -F: 'NF>=3 && $1!=""' | head -n 40 | while IFS=: read -r ssid signal security inuse; do
            s=$signal; case "$s" in ''|*[!0-9]*) s=0;; esac
            [ $first -eq 1 ] || printf ','
            first=0
            printf '{"ssid":"%s","signal":%s,"security":"%s","in_use":%s}' \
                "$(printf '%s' "$ssid" | json_escape)" "$s" \
                "$(printf '%s' "$security" | json_escape)" \
                "$([ "$inuse" = "*" ] && echo true || echo false)"
        done
        printf ']'
        ;;
    connect)
        if [ -z "$SSID" ]; then printf '{"ok":false,"error":"ssid vazio"}'; exit 0; fi
        # Sem eval: ssid/senha vao como argumentos separados para o nmcli.
        if [ -n "$PASSWORD" ]; then
            out=$(nmcli dev wifi connect "$SSID" password "$PASSWORD" 2>&1)
        else
            out=$(nmcli dev wifi connect "$SSID" 2>&1)
        fi
        rc=$?
        if [ $rc -eq 0 ]; then ok=true; else ok=false; fi
        printf '{"ok":%s,"message":"%s"}' "$ok" "$(printf '%s' "$out" | json_escape)"
        ;;
    disconnect)
        out=$(nmcli dev disconnect wlan0 2>&1); rc=$?
        if [ $rc -eq 0 ]; then ok=true; else ok=false; fi
        printf '{"ok":%s,"message":"%s"}' "$ok" "$(printf '%s' "$out" | json_escape)"
        ;;
    forget)
        if [ -z "$SSID" ]; then printf '{"ok":false,"error":"ssid vazio"}'; exit 0; fi
        out=$(nmcli connection delete id "$SSID" 2>&1); rc=$?
        if [ $rc -eq 0 ]; then ok=true; else ok=false; fi
        printf '{"ok":%s,"message":"%s"}' "$ok" "$(printf '%s' "$out" | json_escape)"
        ;;
    *)
        printf '{"ok":false,"error":"acao desconhecida"}'
        ;;
esac
