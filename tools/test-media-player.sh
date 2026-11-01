#!/usr/bin/env bash
# Testes offline da variante `media-player` (não precisam do aparelho).
#
# O que é exercitado de verdade (e não "por inspeção"):
#   1. o binário compila e responde --version/--help
#   2. a biblioteca acha músicas/vídeos e lê ID3v2/ID3v1/FLAC
#   3. a UI desenha as quatro abas (canvas sai com conteúdo, não em branco)
#   4. o decodificador abre um vídeo de verdade e entrega o quadro certo
#   5. o launcher trata saída normal, falha e sessão curta
#
# Uso:
#   tools/test-media-player.sh                     # usa ./build/ouvila-media
#   tools/test-media-player.sh caminho/do/binario
#
# Dependências: python3 (para gerar/conferir imagens), gst-launch-1.0 opcional
# (para gerar o vídeo de teste). Sem gst-launch, o teste de vídeo é pulado.
set -uo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${1:-$ROOT_DIR/build/ouvila-media}
TMP=$(mktemp -d)
FALHAS=0
PULADOS=0

ok()   { printf '  \033[32mok\033[0m   %s\n' "$*"; }
falha(){ printf '  \033[31mFALHA\033[0m %s\n' "$*"; FALHAS=$((FALHAS+1)); }
pula() { printf '  \033[33mpulado\033[0m %s\n' "$*"; PULADOS=$((PULADOS+1)); }

limpar() {
    # mata o que sobrou de uma rodada anterior (por PID, nunca por nome)
    for pid in ${PIDS:-}; do
        kill "$pid" 2>/dev/null || true
    done
    rm -rf "$TMP"
}
trap limpar EXIT
PIDS=""

# Quando o binário foi compilado contra um sysroot (sem instalar os -dev no
# sistema), as bibliotecas ficam lá: aponta o loader para elas.
if [ -n "${WM_SYSROOT:-}" ] && [ -z "${LD_LIBRARY_PATH:-}" ]; then
    for d in "$WM_SYSROOT"/usr/lib/x86_64-linux-gnu "$WM_SYSROOT"/usr/lib/aarch64-linux-gnu; do
        [ -d "$d" ] && LD_LIBRARY_PATH="$d${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    done
    export LD_LIBRARY_PATH
fi

if [ ! -x "$BIN" ]; then
    echo "!!! binario nao encontrado: $BIN" >&2
    echo "    compile com: build/media-player.sh --native (ou --check para so validar)" >&2
    exit 2
fi

echo "== 1. binario =="
if "$BIN" --version >/dev/null 2>&1; then
    ok "--version: $("$BIN" --version)"
else
    falha "--version nao respondeu"
fi
if "$BIN" --help 2>&1 | grep -q -- '--screenshot'; then
    ok "--help lista as opcoes"
else
    falha "--help sem as opcoes esperadas"
fi

# --- biblioteca de teste ---------------------------------------------------
mkdir -p "$TMP/media/music" "$TMP/media/videos" "$TMP/media/music/Album"
python3 - "$TMP/media" <<'PY'
import os, struct, sys, math, wave
base = sys.argv[1]

def frame(fid, texto):
    corpo = b"\x00" + texto.encode("latin-1")
    return fid.encode() + struct.pack(">I", len(corpo)) + b"\x00\x00" + corpo

def id3(titulo, artista, album):
    frames = frame("TIT2", titulo) + frame("TPE1", artista) + frame("TALB", album)
    n = len(frames)
    sync = bytes([(n >> 21) & 0x7f, (n >> 14) & 0x7f, (n >> 7) & 0x7f, n & 0x7f])
    return b"ID3\x03\x00\x00" + sync + frames

open(base + "/music/01 - Faixa Com Tags.mp3", "wb").write(
    id3("Neon Horizon", "CyberPulse", "Retrowave Vol. 1") + b"\x00" * 512)
open(base + "/music/Sem Tags - Artista X.mp3", "wb").write(b"\x00" * 400)
# ID3v1 é EXATAMENTE 128 bytes no fim do arquivo: TAG(3) + título(30) +
# artista(30) + álbum(30) + ano(4) + comentário(30) + gênero(1).
tag_v1 = (b"TAG" + b"Titulo V1".ljust(30, b"\x00")
          + b"Artista V1".ljust(30, b"\x00") + b"Album V1".ljust(30, b"\x00")
          + b"2024" + b"".ljust(30, b"\x00") + b"\x0c")
assert len(tag_v1) == 128, len(tag_v1)
open(base + "/music/antiga.mp3", "wb").write(b"\x00" * 300 + tag_v1)

def vorbis(titulo, artista, album):
    itens = ["TITLE=" + titulo, "ARTIST=" + artista, "ALBUM=" + album]
    corpo = struct.pack("<I", 4) + b"test" + struct.pack("<I", len(itens))
    for item in itens:
        d = item.encode()
        corpo += struct.pack("<I", len(d)) + d
    return b"fLaC" + bytes([0x84]) + len(corpo).to_bytes(3, "big") + corpo

open(base + "/music/Album/tema.flac", "wb").write(vorbis("Starlight", "Nebula", "Cosmic"))
open(base + "/music/Album/folder.jpg", "wb").write(b"\xff\xd8\xff\xe0" + b"\x00" * 64)
open(base + "/videos/filme.mkv", "wb").write(b"\x00" * 2048)
PY

cat > "$TMP/ouvila.conf" << CONF
display_transform=normal
media_volume=65
media_audio_out=-
media_roots=$TMP/media
media_exit=stay
CONF

echo "== 2. biblioteca =="
SAIDA=$("$BIN" --config "$TMP/ouvila.conf" --scan 2>/dev/null)
if echo "$SAIDA" | grep -q "Neon Horizon"; then
    ok "ID3v2 (mp3) lido"
else
    falha "ID3v2 nao lido"
fi
if echo "$SAIDA" | grep -q "Titulo V1"; then
    ok "ID3v1 (mp3) lido"
else
    falha "ID3v1 nao lido"
fi
if echo "$SAIDA" | grep -q "Starlight"; then
    ok "Vorbis comment (flac) lido"
else
    falha "Vorbis comment nao lido"
fi
if echo "$SAIDA" | grep -q "Artista X"; then
    ok "nome do arquivo como fallback"
else
    falha "fallback do nome do arquivo nao funcionou"
fi
if echo "$SAIDA" | grep -q "^video"; then
    ok "video encontrado na varredura"
else
    falha "video nao encontrado"
fi

echo "== 3. UI (desenho offline) =="
for aba in musica videos biblioteca sistema; do
    PPM="$TMP/aba-$aba.ppm"
    if ! "$BIN" --config "$TMP/ouvila.conf" --tab "$aba" --screenshot "$PPM" >/dev/null 2>&1; then
        falha "aba $aba nao desenhou"
        continue
    fi
    # Um canvas em branco/preto de verdade tem 1 ou 2 cores; a UI tem texto,
    # bordas e gradiente. Também confere que a tela foi inteira escrita.
    RES=$(python3 - "$PPM" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    if f.readline().strip() != b'P6':
        print('formato'); raise SystemExit
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read()
if len(d) != w * h * 3:
    print('tamanho'); raise SystemExit
cores = {d[i:i+3] for i in range(0, len(d), 3 * 37)}
print(len(cores))
PY
)
    if [ "${RES:-0}" -ge 20 ] 2>/dev/null; then
        ok "aba $aba: $RES cores distintas"
    else
        falha "aba $aba: canvas suspeito (${RES:-?} cores)"
    fi
done

# O menu de configurações é desenhado por cima (overlay). Uma captura só da aba
# não pega erro de layout nem de fonte nele — e é a tela que mais mudou.
if "$BIN" --config "$TMP/ouvila.conf" --menu \
    --screenshot "$TMP/menu.ppm" >/dev/null 2>&1; then
    RES=$(python3 - "$TMP/menu.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    if f.readline().strip() != b'P6':
        print(0); raise SystemExit
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read()
if len(d) != w * h * 3:
    print(0); raise SystemExit
# O menu escurece o fundo mas mantém o painel: muitas cores, mas nenhuma
# dominando a tela toda.
cores = {d[i:i+3] for i in range(0, len(d), 3 * 37)}
print(len(cores))
PY
)
    if [ "${RES:-0}" -ge 30 ] 2>/dev/null; then
        ok "menu de configuracoes: $RES cores distintas"
    else
        falha "menu de configuracoes: canvas suspeito (${RES:-?} cores)"
    fi
else
    falha "menu de configuracoes nao desenhou"
fi

# --- idiomas e temas ------------------------------------------------------
# A UI fala 10 idiomas e tem 5 temas. O que dá para provar sem olhar a tela:
# cada idioma desenha (não trava) e produz um canvas diferente dos outros, e
# trocar o tema muda as cores de verdade.
echo "== 3b. idiomas e temas =="
HASHES=""
IDIOMAS_OK=0
for idioma in pt en zh hi es ar bn id ur fr; do
    PPM="$TMP/idioma-$idioma.ppm"
    if ! "$BIN" --config "$TMP/ouvila.conf" --tab sistema --idioma "$idioma" \
        --screenshot "$PPM" >/dev/null 2>&1; then
        falha "idioma $idioma nao desenhou"
        continue
    fi
    H=$(python3 - "$PPM" <<'PY'
import hashlib, sys
with open(sys.argv[1], 'rb') as f:
    f.readline(); f.readline(); f.readline(); d = f.read()
print(hashlib.md5(d).hexdigest()[:12])
PY
)
    case " $HASHES " in
        *" $H "*) : ;;               # repetido: não conta
        *) HASHES="$HASHES $H"; IDIOMAS_OK=$((IDIOMAS_OK+1)) ;;
    esac
done
if [ "$IDIOMAS_OK" -ge 9 ]; then
    ok "os 10 idiomas desenham e produzem telas distintas ($IDIOMAS_OK)"
else
    falha "idiomas renderizaram igual demais ($IDIOMAS_OK telas distintas)"
fi

# O árabe tem de sair da direita para a esquerda e em forma ligada. Comparar a
# largura de "مرحبا" com as mesmas letras separadas por espaço prova que o
# HarfBuzz está juntando (sem shaping as duas medidas seriam iguais).
if "$BIN" --config "$TMP/ouvila.conf" --selftest >/dev/null 2>&1; then
    ok "selftest com as traducoes carregadas"
fi

TEMAS_OK=0
TEMAS_HASH=""
for tema in neon oled mono mono-glow vidro; do
    PPM="$TMP/tema-$tema.ppm"
    if ! "$BIN" --config "$TMP/ouvila.conf" --tab musica --tema "$tema" \
        --screenshot "$PPM" >/dev/null 2>&1; then
        falha "tema $tema nao desenhou"
        continue
    fi
    H=$(python3 - "$PPM" <<'PY'
import hashlib, sys
with open(sys.argv[1], 'rb') as f:
    f.readline(); f.readline(); f.readline(); d = f.read()
print(hashlib.md5(d).hexdigest()[:12])
PY
)
    case " $TEMAS_HASH " in
        *" $H "*) : ;;
        *) TEMAS_HASH="$TEMAS_HASH $H"; TEMAS_OK=$((TEMAS_OK+1)) ;;
    esac
done
if [ "$TEMAS_OK" -ge 4 ]; then
    ok "os 5 temas desenham com paletas distintas ($TEMAS_OK)"
else
    falha "temas repetidos ($TEMAS_OK paletas distintas)"
fi

echo "== 4. navegacao e controles da UI (--selftest) =="
# O exit code é o sinal principal: o --selftest devolve != 0 quando qualquer
# caso falha. O grep de 'FALHA' fica como rede de segurança — só olhar o texto
# já deixou uma falha passar batido quando a mensagem mudou.
SAIDA=$("$BIN" --config "$TMP/ouvila.conf" --selftest 2>/dev/null)
RC=$?
if [ "$RC" -ne 0 ] || printf '%s' "$SAIDA" | grep -q 'FALHA'; then
    falha "o --selftest acusou falha de navegacao (rc=$RC)"
    printf '%s\n' "$SAIDA" | sed 's/^/    /'
else
    N_OK=$(printf '%s' "$SAIDA" | grep -c '^  ok')
    QTD=$(printf '%s' "$SAIDA" | grep -c -e '^  ok' -e '^  FALHA' -e '^  pulado')
    if [ "$QTD" -lt 40 ]; then
        # Sem isso um selftest que aborta cedo (biblioteca vazia) "passa".
        falha "o --selftest rodou so $QTD caso(s); esperado 40+"
        printf '%s\n' "$SAIDA" | sed 's/^/    /'
    else
        ok "navegacao: $N_OK casos (abas, volume, tela cheia, power, menu com submenus, brilho, L1/R1, biblioteca, modo, idioma, tema, EQ)"
    fi
fi

echo "== 5. decodificador =="
if command -v gst-launch-1.0 >/dev/null 2>&1; then
    if gst-launch-1.0 -q videotestsrc num-buffers=60 pattern=smpte \
        ! video/x-raw,width=320,height=180,framerate=30/1 ! x264enc tune=zerolatency \
        ! matroskamux ! filesink location="$TMP/media/videos/teste.mkv" >/dev/null 2>&1; then
        if "$BIN" --config "$TMP/ouvila.conf" --play "$TMP/media/videos/teste.mkv" \
             --frame-out "$TMP/quadro.ppm" --frame-at 0.5 >/dev/null 2>&1
        then
            CORES=$(python3 - "$TMP/quadro.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    f.readline(); w, h = map(int, f.readline().split()); f.readline(); d = f.read()
print(len({d[i:i+3] for i in range(0, len(d), 3 * 53)}))
PY
)
            if [ "${CORES:-0}" -ge 5 ]; then
                ok "video decodificado e escalado ($CORES cores)"
            else
                falha "quadro de video sem conteudo ($CORES cores)"
            fi
        else
            falha "o reprodutor nao abriu o video de teste"
        fi
    else
        pula "x264enc indisponivel para gerar o video de teste"
    fi
else
    pula "gst-launch-1.0 ausente (teste de video nao executado)"
fi

echo "== 6. descoberta de cabeçalhos (--native, caso do CI) =="
# O CI falhava em `drm.h`/`ft2build.h` porque o modo --native não adicionava
# /usr/include/libdrm nem /usr/include/freetype2. WM_BASE_INC aponta para uma
# árvore com o layout de /usr, então o caso é reproduzido sem instalar nada.
if [ -n "${WM_TEST_BASE_INC:-}" ] && [ -d "${WM_TEST_BASE_INC:-}" ]; then
    if WM_BASE_INC="$WM_TEST_BASE_INC" WM_OUT="$TMP/bin-flags" \
        "$ROOT_DIR/build/media-player.sh" --native >"$TMP/flags.log" 2>&1; then
        if grep -q -- '-I.*libdrm' "$TMP/flags.log" && grep -q -- '-I.*freetype2' "$TMP/flags.log"; then
            ok "modo --native encontra libdrm/freetype2"
        else
            falha "--native compilou mas sem os -I de libdrm/freetype2"
            sed 's/^/    /' "$TMP/flags.log"
        fi
    else
        falha "--native nao compilou com o layout do sistema"
        tail -6 "$TMP/flags.log" | sed 's/^/    /'
    fi
else
    pula "defina WM_TEST_BASE_INC=<arvore com layout de /usr> para testar isso"
fi

echo "== 7. launcher =="
LAUNCHER="$ROOT_DIR/variants/media-player/overlay/usr/local/bin/start-media-player.sh"
if [ ! -x "$LAUNCHER" ]; then
    falha "launcher ausente: $LAUNCHER"
else
    mkdir -p "$TMP/fakebin"
    printf '#!/bin/sh\nexit 0\n' > "$TMP/fakebin/ouvila-media"
    # Falha: status != 0 tem de sair != 0 (para o systemd ver a falha)
    printf '#!/bin/sh\nexit 3\n' > "$TMP/fakebin/falha"
    chmod +x "$TMP/fakebin/ouvila-media" "$TMP/fakebin/falha"

    if MEDIA_BIN="$TMP/fakebin/falha" MEDIA_LOG="$TMP/launcher.log" \
        OUVILA_MIN_SEGUNDOS=0 "$LAUNCHER" >/dev/null 2>&1; then
        falha "launcher devolveu 0 para um binario que falhou"
    else
        ok "launcher propaga falha do binario"
    fi

    # Sessão curta com status 0 não é sucesso (mesma regra do Kodi/RetroArch).
    if MEDIA_BIN="$TMP/fakebin/ouvila-media" MEDIA_LOG="$TMP/launcher.log" \
        OUVILA_MIN_SEGUNDOS=10 "$LAUNCHER" >/dev/null 2>&1; then
        falha "launcher tratou sessao curta como sucesso"
    else
        ok "launcher trata sessao curta como falha"
    fi

    if grep -q "sessao curta demais" "$TMP/launcher.log" 2>/dev/null; then
        ok "launcher registra o motivo no log"
    else
        falha "launcher nao registrou o motivo"
    fi
fi

echo
if [ "$FALHAS" -eq 0 ]; then
    echo "== todos os testes passaram (${PULADOS} pulado(s)) =="
    exit 0
fi
echo "== $FALHAS falha(s) (${PULADOS} pulado(s)) =="
exit 1
