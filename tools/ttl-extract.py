#!/usr/bin/env python3
"""ttl-extract.py — extrai o hardware de um aparelho pelo console serial.

Pressupõe que o aparelho está ligado com a console em TTY e com **shell de root
aberto** (autologin). Não instala nada e não escreve no aparelho: só lê.

O que sai:
    <destino>/<placa>-live.dtb      device tree VIVO (/sys/firmware/fdt)
    <destino>/<placa>-live.dts      o mesmo, decodificado (via tools/fdt-dump.py)
    <destino>/relatorio.txt         tudo o que os comandos responderam
    <destino>/hardware.md           resumo legível (modelo, painel, botões, ...)

Por que o FDT vivo é o dado mais valioso: num aparelho de fábrica o DTS não é
publicado; o blob que o kernel recebeu do bootloader é a descrição exata do
hardware daquela unidade (painel, GPIOs, ADC, slots, reguladores).

Dependências: pyserial (`pip install pyserial`).
No host sem permissão na porta serial, rode dentro de um container com
`--device=/dev/ttyUSB0` (foi como este script foi validado).

Uso:
    ttl-extract.py --porta /dev/ttyUSB0 --baud 1500000 --destino saida/ --placa r35s
"""
import argparse
import base64
import gzip
import os
import re
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    raise SystemExit("falta pyserial: pip install pyserial")

ANSI = re.compile(rb"\x1b\[[0-9;]*m")

# Cada comando tem um marcador próprio: assim o parse não depende de prompt
# (cada aparelho usa um PS1 diferente, às vezes com cor).
COMANDOS = [
    ("ident", "echo MARK_IDENT; "
              "cat /proc/device-tree/model; echo; "
              "tr '\\0' '\\n' < /proc/device-tree/compatible; echo; "
              "uname -a; cat /proc/cmdline"),
    ("cpu", "echo MARK_CPU; "
            "echo nucleos=$(grep -c ^processor /proc/cpuinfo); "
            "grep -m1 'CPU part' /proc/cpuinfo; "
            "grep -E 'MemTotal|MemAvailable' /proc/meminfo"),
    ("drm", "echo MARK_DRM; "
            "for c in /sys/class/drm/card*/card*-*; do "
            "[ -e \"$c/status\" ] || continue; echo \"$c $(cat $c/status)\"; "
            "head -3 $c/modes; done"),
    ("backlight", "echo MARK_BL; ls /sys/class/backlight/ 2>/dev/null; "
                  "for d in /sys/class/backlight/*; do "
                  "echo \"nome=$(basename $d) atual=$(cat $d/brightness 2>/dev/null) "
                  "max=$(cat $d/max_brightness 2>/dev/null)\"; done"),
    ("input", "echo MARK_INPUT; cat /proc/bus/input/devices"),
    ("caps", "echo MARK_CAPS; for e in /sys/class/input/event*; do "
             "echo \"-- $e\"; cat $e/device/name 2>/dev/null; "
             "echo \"key=$(cat $e/device/capabilities/key 2>/dev/null)\"; "
             "echo \"abs=$(cat $e/device/capabilities/abs 2>/dev/null)\"; done"),
    ("som", "echo MARK_SOM; cat /proc/asound/cards 2>/dev/null; "
            "cat /proc/asound/pcm 2>/dev/null | head -6"),
    ("energia", "echo MARK_PWR; for p in /sys/class/power_supply/*; do "
                "[ -e \"$p/type\" ] || continue; echo \"-- $p\"; cat $p/type; "
                "cat $p/capacity $p/status 2>/dev/null; done"),
    ("armazenamento", "echo MARK_BLK; cat /proc/partitions; "
                      "echo --; ls /sys/class/mmc_host/ 2>/dev/null; "
                      "for h in /sys/class/mmc_host/*; do "
                      "echo \"$h $(cat $h/mmc*/name 2>/dev/null) "
                      "$(cat $h/mmc*/type 2>/dev/null)\"; done"),
    ("usb", "echo MARK_USB; ls /sys/class/udc/ 2>/dev/null; "
            "cat /sys/class/usb_role/*/role 2>/dev/null"),
    ("radio", "echo MARK_RADIO; ls /sys/class/ieee80211 2>/dev/null; "
              "ls /sys/class/bluetooth 2>/dev/null; "
              "cat /sys/class/mmc_host/mmc*/mmc*:*/name 2>/dev/null"),
    ("dmesg", "echo MARK_DMESG; dmesg 2>/dev/null | "
              "grep -iE 'panel|dsi|drm|mmc[0-9]|dwmmc|joypad|saradc|rk817|dwc|udc' "
              "| head -40"),
]

MARCA_INI, MARCA_FIM = "INICIO_FDT_TTLX", "FIM_FDT_TTLX"


class Console:
    def __init__(self, porta, baud):
        self.link = serial.Serial(porta, baud, bytesize=8, parity="N", stopbits=1,
                                  timeout=0.2, rtscts=False, dsrdtr=False)
        self.link.reset_input_buffer()

    def acordar(self):
        self.link.write(b"\r")
        self.link.flush()
        time.sleep(0.4)
        self.ler(1.5)

    def sem_eco(self, desligar=True):
        """Desliga o eco do terminal: sem isso o parse pega o comando de volta.

        O console do aparelho ecoa cada byte digitado, e a linha de 80 colunas
        quebra o eco no meio das palavras — não dá para distinguir "eco do
        comando" de "saída do comando" por texto. Com `stty -echo` o terminal
        para de devolver o que enviamos e os marcadores ficam confiáveis.
        """
        self.link.write(("stty %secho\r" % ("-" if desligar else "")).encode())
        self.link.flush()
        time.sleep(0.3)
        self.ler(0.8)

    def ler(self, segundos, ocioso_max=1.2):
        """Lê até o tempo acabar ou a linha ficar quieta."""
        fim = time.monotonic() + segundos
        ultimo = time.monotonic()
        buf = bytearray()
        while time.monotonic() < fim:
            bloco = self.link.read(4096)
            if bloco:
                buf += bloco
                ultimo = time.monotonic()
            elif time.monotonic() - ultimo > ocioso_max:
                break
        return bytes(buf)

    def comando(self, cmd, segundos):
        self.link.write(cmd.encode() + b"\r")
        self.link.flush()
        time.sleep(0.25)
        return self.ler(segundos)

    def fdt(self, destino, segundos=25):
        """Traz /sys/firmware/fdt comprimido (57 KB viram ~15 KB na serial)."""
        self.link.write(("echo %s; gzip -9c /sys/firmware/fdt | base64; echo %s\r"
                         % (MARCA_INI, MARCA_FIM)).encode())
        self.link.flush()
        bruto = self.ler(segundos, ocioso_max=2.0)
        return destilar_fdt(bruto, destino)


def limpar(bruto):
    return ANSI.sub(b"", bruto).replace(b"\r", b"")


def destilar_fdt(bruto, destino):
    """Recorta o base64 do gzip e grava o .dtb. Devolve o caminho ou None."""
    d = limpar(bruto)
    # O marcador aparece duas vezes: no eco do comando e na saída. O payload
    # começa sempre em "H4sI" (cabeçalho do gzip em base64).
    k = d.find(b"H4sI")
    if k < 0:
        return None
    pedacos, i = [], k
    while i < len(d):
        c = d[i:i + 1]
        if c in (b"\n", b" "):
            i += 1
            continue
        if re.fullmatch(rb"[A-Za-z0-9+/=]", c):
            pedacos.append(c)
            i += 1
            continue
        break
    try:
        fdt = gzip.decompress(base64.b64decode(b"".join(pedacos)))
    except Exception as erro:                                  # noqa: BLE001
        print("!!! base64/gzip do FDT falhou: %s" % erro, file=sys.stderr)
        return None
    if fdt[:4] != b"\xd0\x0d\xfe\xed":
        print("!!! o que veio nao e um DTB", file=sys.stderr)
        return None
    with open(destino, "wb") as f:
        f.write(fdt)
    return destino


def bloco_saida(texto, marca):
    """Recorta a saída entre o marcador e o próximo MARK_.

    Só funciona com o echo do terminal desligado (ver Console.sem_eco): com o
    eco ligado o primeiro marcador encontrado é o do comando, não o da saída.
    """
    i = texto.find(marca)
    if i < 0:
        return ""
    i = texto.find("\n", i)
    if i < 0:
        return ""
    j = texto.find("MARK_", i)
    corpo = texto[i + 1:j if j > 0 else len(texto)]
    # O prompt de cada aparelho aparece de duas formas e as duas sujam o bloco:
    #   - sozinho numa linha (`root@x:/# `) ou
    #   - no INÍCIO de uma linha, quando uma mensagem periódica do sistema foi
    #     impressa em seguida (`giochi-system:/# energia: bateria sem medidor`).
    # Por isso o prompt é removido de qualquer linha e, se sobrar só a mensagem
    # periódica, a linha inteira é descartada.
    saida = []
    for linha in corpo.splitlines():
        if re.search(r"\S*:/[#$]\s*$", linha):
            continue
        linha = re.sub(r"^\S*:/[#$]\s", "", linha)
        if not linha.strip() or linha.startswith("energia:"):
            continue
        saida.append(linha)
    return "\n".join(saida)


def resumo_hardware(blocos, dtb):
    """Monta um hardware.md a partir do que foi lido no aparelho."""
    linhas = ["# Hardware lido do aparelho por console serial\n"]

    ident = [l.strip() for l in blocos.get("ident", "").splitlines() if l.strip()]
    if ident:
        linhas.append("## Identificação\n")
        linhas.append("- **modelo (device tree)**: %s" % ident[0])
        # O `tr` da lista compatible termina sem separador: para de coletar no
        # primeiro item que já é de outra seção (kernel ou cmdline).
        comp = []
        for l in ident[1:]:
            if "," not in l or l.startswith("Linux version") or "root=" in l:
                break
            comp.append(l)
        if comp:
            linhas.append("- **compatible**: `%s`" % "`, `".join(comp))
        for l in ident:
            if l.startswith("Linux version"):
                linhas.append("- **kernel**: %s" % l)
            if "root=" in l or "console=" in l:
                linhas.append("- **cmdline**: `%s`" % l)
        linhas.append("")

    cpu = [l.strip() for l in blocos.get("cpu", "").splitlines() if l.strip()]
    if cpu:
        nucleos = next((l.split("=")[1] for l in cpu if l.startswith("nucleos=")), "?")
        particula = next((l.split(":")[1].strip() for l in cpu if "CPU part" in l), "?")
        nomes = {"0xd03": "Cortex-A53", "0xd04": "Cortex-A35", "0xd08": "Cortex-A72",
                 "0xd0b": "Cortex-A76", "0xd05": "Cortex-A55"}
        linhas.append("## CPU e memória\n")
        linhas.append("- **núcleos**: %s · **CPU part**: %s (%s)"
                      % (nucleos, particula, nomes.get(particula, "desconhecido")))
        for l in cpu:
            if l.startswith("MemTotal"):
                linhas.append("- **memória**: %s" % l.split(":")[1].strip())
        linhas.append("")

    drm = blocos.get("drm", "").strip()
    if drm:
        linhas.append("## Vídeo\n")
        for l in drm.splitlines():
            if "connected" in l or re.match(r"^\d+x\d+$", l.strip()) or "disconnected" in l:
                linhas.append("- `%s`" % l.strip())
        linhas.append("")

    bl = [l.strip() for l in blocos.get("backlight", "").splitlines() if l.strip()]
    linhas.append("## Backlight\n")
    if bl:
        for l in bl:
            if l.startswith("nome="):
                linhas.append("- `%s`" % l)
    else:
        linhas.append("- nenhum backlight em /sys/class/backlight")
    linhas.append("")

    inp = blocos.get("input", "")
    if inp.strip():
        linhas.append("## Entrada (evdev)\n")
        nome = None
        for l in inp.splitlines():
            if l.startswith("N: Name="):
                nome = l.split("=", 1)[1].strip()
            elif l.startswith("H: Handlers="):
                linhas.append("- **%s** → `%s`" % (nome, l.split("=", 1)[1].strip()))
        linhas.append("")

    caps = blocos.get("caps", "")
    if caps.strip():
        linhas.append("### Capacidades (bitmaps do kernel, para montar o mapa de botões)\n")
        linhas.append("```")
        linhas.extend(l.rstrip() for l in caps.strip().splitlines())
        linhas.append("```\n")

    # Seção vazia também informa: "nenhum power_supply" e "sem rádio" são
    # resultados, não ausência de medição.
    vazios = {
        "som": "nenhuma placa de som registrada",
        "energia": "nenhum `/sys/class/power_supply` (o kernel em uso não tem driver de bateria)",
        "radio": "nenhum adaptador Wi-Fi/Bluetooth no kernel em uso",
    }
    for chave, titulo in (("som", "Áudio (ALSA)"), ("energia", "Energia"),
                          ("armazenamento", "Armazenamento"), ("usb", "USB"),
                          ("radio", "Rádio (Wi-Fi/BT)")):
        corpo = blocos.get(chave, "").strip()
        linhas.append("## %s\n" % titulo)
        if corpo:
            linhas.append("```")
            linhas.extend(corpo.splitlines())
            linhas.append("```\n")
        else:
            linhas.append("- %s\n" % vazios.get(chave, "nada encontrado"))

    dm = blocos.get("dmesg", "").strip()
    if dm:
        linhas.append("## dmesg (linhas de hardware)\n")
        linhas.append("```")
        linhas.extend(dm.splitlines())
        linhas.append("```\n")

    if dtb:
        linhas.append("## Device tree vivo\n")
        linhas.append("- binário: `%s`" % os.path.basename(dtb))
        linhas.append("- decodifique com `tools/fdt-dump.py <arquivo>.dtb dts`")
    return "\n".join(linhas) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=1500000,
                    help="1500000 no R35S/R36S; 1500000 tambem na X55 (ttyS2)")
    ap.add_argument("--destino", default="saida-ttl")
    ap.add_argument("--placa", default="placa")
    ap.add_argument("--segundos", type=float, default=6.0,
                    help="espera por comando (aumente se o aparelho for lento)")
    args = ap.parse_args(argv)

    os.makedirs(args.destino, exist_ok=True)
    relatorio = os.path.join(args.destino, "relatorio.txt")
    blocos = {}

    con = Console(args.porta, args.baud)
    try:
        con.acordar()
        con.sem_eco(True)
        with open(relatorio, "wb") as f:
            for nome, cmd in COMANDOS:
                print("--> %s" % nome)
                saida = con.comando(cmd, args.segundos)
                f.write(b"\n=== %s ===\n" % nome.encode() + saida)
                f.flush()
                blocos[nome] = bloco_saida(limpar(saida).decode("utf-8", "replace"),
                                           "MARK_" + {
                                               "ident": "IDENT", "cpu": "CPU", "drm": "DRM",
                                               "backlight": "BL", "input": "INPUT",
                                               "caps": "CAPS", "som": "SOM",
                                               "energia": "PWR", "armazenamento": "BLK",
                                               "usb": "USB", "radio": "RADIO",
                                               "dmesg": "DMESG"}[nome])
            print("--> device tree vivo (comprimido)")
            dtb = con.fdt(os.path.join(args.destino, "%s-live.dtb" % args.placa))
            f.write(b"\n=== fdt ===\nDTB: %s\n" % (dtb or "FALHOU").encode())
    finally:
        try:
            con.sem_eco(False)
        except Exception:                                       # noqa: BLE001
            pass
        con.link.close()

    dts = None
    if dtb:
        dts = os.path.splitext(dtb)[0] + ".dts"
        ferramenta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fdt-dump.py")
        if os.path.exists(ferramenta):
            with open(dts, "w") as f:
                subprocess.run([sys.executable, ferramenta, dtb, "dts"],
                               stdout=f, check=False)
        print("DTB: %s (%d bytes)" % (dtb, os.path.getsize(dtb)))

    with open(os.path.join(args.destino, "hardware.md"), "w") as f:
        f.write(resumo_hardware(blocos, dtb))

    print("pronto: %s (relatorio.txt, hardware.md%s)"
          % (args.destino, ", %s-live.dtb/.dts" % args.placa if dtb else ""))
    return 0 if dtb else 1


if __name__ == "__main__":
    sys.exit(main())
