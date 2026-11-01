# Variantes: `kodi` (multimídia), `retroarch` (jogos) e `media-player`

O projeto gera **três imagens** a partir do mesmo kernel, U-Boot e device tree. A
diferença é o que entra no rootfs e qual serviço toma o `tty1` no boot.

| | `kodi` | `retroarch` | `media-player` |
| --- | --- | --- | --- |
| Para quê | vídeo, música, streaming | jogos (libretro) | música e vídeo com UI própria |
| App principal | Kodi 20 | RetroArch 1.14 | **`ouvila-media`** (binário único, C) |
| Camadas usadas | Weston + Kodi | Weston + RetroArch | **nenhuma** — DRM/KMS + FFmpeg + ALSA |
| Serviço no tty1 | `kodi.service` | `retroarch.service` | `media-player.service` |
| Artefato | `rk3566-x55-mainline-sdcard-image` | `rk3566-x55-retroarch-sdcard-image` | `rk3566-x55-media-player-sdcard-image` |
| Add-on de rádio no app | `script.ouvila.radio` | — (painel web) | Wi-Fi/BT no **menu** (não há aba de rádio: estes aparelhos não têm AM/FM) |
| Cores | — | NES, SNES, GB/GBC, GBA, PC Engine, PS1 | — |
| Status | validado no X55 e X35H | build #116 verde; hardware parcial | **X35H: app sobe, desenha e toca. v0.3: menu com submenus, biblioteca por álbum, 10 idiomas, 5 temas, equalizador** |

O que é **igual** nas três: kernel, U-Boot, DTBs (X55 + X35S/X35H), rotação
(`display_transform`), rede USB (`g_ether`, 10.55.0.1), SSH `root/root`, painel web de
rádio na **8088**, `radio-retry`, `usb-watch` e os logs de diagnóstico no cartão.

## Como escolher a variante no build

**Por push na `main`:** a variante é `kodi` (padrão), para não quebrar o fluxo usado
para testar no aparelho.

**Manual:** Actions → *Build Mainline RK3566 (Powkiddy X55) OS* → **Run workflow** →
campo **variant** → `kodi`, `retroarch` ou `media-player`.

**Por tag:** uma tag que comece com `retroarch-` constrói a variante de jogos e uma que
comece com `media-player-` constrói a de mídia; as duas também servem para marcar uma
versão:

```sh
git tag retroarch-v1 && git push origin retroarch-v1
```

**Localmente** (empacotando artefatos já preparados):

```sh
build/build.sh powkiddy-x55 kodi
build/build.sh powkiddy-x55 retroarch
```

O `variant.conf` declara `status=available` e o manifesto de pacotes; variantes com
outro `status` são recusadas pelo empacotador.

## Como o build trata a variante

O passo *Build Minimal RootFS* instala a base, depois entra num bloco por variante:

* **`kodi`**: `kodi`, `kodi-bin`, `kodi-peripheral-joystick`, Weston, mesa/EGL,
  `alsa-utils`; mapa de controle do gamepad ROCKNIX; `start-kodi-wayland.sh`;
  `kodi.service`; `weston.ini`; add-on `script.ouvila.radio` (com checagem de sintaxe
  Python no build).
* **`retroarch`**: pacotes lidos de `variants/retroarch/packages/retroarch.list`;
  descoberta do diretório real dos cores (o Debian usa `/usr/lib/<triple>/libretro`) e
  do binário `retroarch`; geração do `retroarch.cfg` a partir do modelo em
  `variants/retroarch/overlay/etc/ouvila/retroarch.cfg` com o caminho real
  substituído; criação de `/storage/{roms,saves,states,screenshots,bios}`;
  `start-retroarch.sh` + `retroarch.service`.
* **`media-player`**: pacotes lidos de `variants/media-player/packages/media-player.list`
  (só bibliotecas: FFmpeg 5.1, libdrm, ALSA, FreeType, fontes); os cabeçalhos arm64 do
  **bookworm** são baixados e extraídos num sysroot temporário e o app é compilado com
  `build/media-player.sh` (cross `aarch64-linux-gnu-gcc`, conferindo que o binário é
  `ARM aarch64`); instala `usr/local/bin/ouvila-media` e `media-player.service`, cria
  `/storage/media/{music,videos}` e **executa o binário com `--version`** dentro do
  rootfs (via qemu) para pegar biblioteca faltando antes de gravar a imagem.

O build **falha** se: o `retroarch` não for instalado, o diretório de cores não
existir, houver menos de 5 cores, o `libretro_directory` não tiver sido substituído no
`retroarch.cfg`, ou (media-player) faltar uma biblioteca/fonte no rootfs ou o binário
não executar. Melhor quebrar no CI do que descobrir no aparelho.

## A variante `media-player`: um binário só

As outras duas variantes empilham camadas (Weston → app). A `media-player` existe para
o caminho oposto: **um único processo em C** que é a camada inteira.

```text
ouvila-media (um binário)
├── DRM/KMS ....... desenha o framebuffer e faz page flip (sem compositor)
├── libav* ........ demux + decode de áudio/vídeo (o mesmo FFmpeg do Debian)
├── ALSA .......... escreve S16 no codec rk817 (sem servidor de áudio)
├── FreeType ...... texto da UI
└── evdev ......... gamepad `rocknix-joypad` + teclado USB
```

Não há Weston, `cog`/WebKit, `mpv`, servidor HTTP interno, Node ou Python. O código
mora em [`variants/media-player/src`](../variants/media-player/src) e é compilado no
runner (cross `aarch64-linux-gnu-gcc`, cabeçalhos do **bookworm arm64** — é o que
garante compatibilidade com o FFmpeg 5.1 da imagem).

### Abas (4, com L/R)

| Aba | O que faz |
| --- | --- |
| **Música** | capa do álbum (`folder.jpg`/`cover.png`), título/artista/álbum, barra de progresso, controles (com o botão de **modo**) e a lista de faixas |
| **Vídeos** | grade da biblioteca; ao escolher, reproduz **em tela cheia** com OSD (progresso e dicas) |
| **Biblioteca** | navegação **por álbum**: a lista da esquerda são os álbuns (com contagem de faixas); A abre o álbum e mostra as faixas à direita, onde A toca. B/◀ volta para a lista de álbuns |
| **Sistema** | **só informação**: modelo, uptime, bateria, volume, ALSA, armazenamento e brilho atual. Nada de configuração aqui |

A aba **Rádio foi removida** (v0.3): estes aparelhos não têm rádio AM/FM, e Wi-Fi e
Bluetooth passaram a ser itens do menu. O lugar dela na barra virou a Biblioteca.

Na aba Sistema não se configura nada de propósito: ali ficam apenas os dados do
aparelho, e o botão de volume do hardware não corre o risco de mexer no brilho (foi um
bug no aparelho).

### Menu de configurações (SELECT)

O menu tem uma raiz e **submenus**. ◀ ▶ muda o valor, A confirma/entra, B volta um
nível (no equalizador, o B volta para o Som).

| Item | O que faz |
| --- | --- |
| **Brilho** | ◀ ▶ em passos de 5%, com **piso de 10%** (abaixo disso o painel fica ilegível e parece travado). Para economizar bateria de verdade existe "Apagar a tela" |
| **Wi-Fi** | procura redes, lista SSID/sinal/segurança; rede com senha abre o **teclado na tela**; rede aberta conecta direto |
| **Bluetooth** | liga/desliga o controlador, busca dispositivos e conecta pelo MAC da lista |
| **Som** | predefinição do equalizador, pré-amplificação, **equalizador de 10 bandas**, melhoria de som (modo neural) e restaurar padrão |
| **Idioma** | 10 idiomas; a escolha é gravada e vale na hora |
| **Relógio** | ano/mês/dia/hora/minuto com ◀ ▶ (o dia respeita o mês e o ano bissexto) e "Aplicar data e hora" |
| **Tema** | os 5 temas; ◀ ▶ pré-visualiza ao vivo |
| **Apagar a tela** | DPMS off (mesmo efeito do toque no power) |
| **Desligar / Reiniciar** | diálogo de confirmação; o foco começa em **Cancelar** |
| **Fechar menu** | ou B |

Tudo o que o menu muda é gravado em **`/boot/ouvila-media.conf`** (o próprio app
escreve; o `ouvila.conf` continua sendo editado à mão no cartão):
`idioma`, `tema`, `modo` (modo de reprodução), `eq0`..`eq9`, `eq_preset`, `preamp`,
`neural`.

### Modos de reprodução

O quinto botão da barra de controles (◀ ▶ até ele, A muda) gira entre:

| Modo | No fim da faixa |
| --- | --- |
| em ordem | toca a próxima; **para** quando chega ao fim da lista |
| repetir álbum | toca a próxima, dando a volta no fim |
| repetir faixa | toca a mesma de novo |
| aleatória | sorteia uma faixa de áudio da biblioteca |

O modo também é gravado nos ajustes (`modo=0..3`), então sobrevive a um reboot.

### Equalizador (menu → Som)

10 bandas de oitava (31, 62, 125, 250, 500, 1k, 2k, 4k, 8k e 16k Hz), ±12 dB, mais
pré-amplificação de −12 a +12 dB. Oito predefinições (Plano, Rock, Pop, Jazz, Clássico,
Voz, Graves, Eletrônico) e "Personalizado", que é o que o usuário montou.

É tudo software, no caminho do áudio, **antes** do `snd_pcm_writei`: filtros *peaking*
(RBJ) em cascata por canal, pré-amplificação e um limitador suave (só entra quando
alguma banda soma ganho). Com o preset Plano e preamp 0 o processamento é pulado — não
custa CPU.

**Melhoria de som (modo neural)**: o RK3566 tem NPU, mas o driver só existe no kernel
de fornecedor (`/dev/rknpu`). No mainline esse dispositivo não existe, então o item
aparece **desativado** e com a explicação na tela — foi o que o usuário pediu ("em caso
de não disponibilidade o botão não poderá ser ativado"). No dia em que o kernel trouxer
o NPU, o item passa a aceitar ser ligado sem mudar o app.

### Idiomas

Português (Brasil), inglês, chinês mandarim, hindi, espanhol, árabe padrão moderno,
bengali, indonésio, urdu e francês. Os textos ficam em
[`src/i18n.c`](../variants/media-player/src/i18n.c), indexados pelo enum `MSG_*` do
`wm.h`; o que faltar cai no português (nunca aparece ponteiro vazio).

Três desses alfabetos não funcionam com "um glifo por caractere":
**árabe e urdu juntam as letras** e **hindi e bengali reordenam as vogais**. Quem faz
isso é o **HarfBuzz** ([`src/font.c`](../variants/media-player/src/font.c)); o FreeType
só rasteriza o índice de glifo que o HarfBuzz escolheu. Como a DejaVu não tem árabe,
devanágari, bengali nem chinês, cada `Font` carrega faces de reserva e o texto é fatiado
por alfabeto — o mesmo rótulo pode misturar "Wi-Fi " (DejaVu) com "شبكة" (Noto Arabic).

### Temas

Os cinco protótipos de [`layout/`](../variants/media-player/layout) viraram dados em
[`src/tema.c`](../variants/media-player/src/tema.c): **Neon** (azul-noite + rosa),
**OLED vermelho**, **Monocromático**, **Mono com brilho** (o mesmo preto/branco com
realce forte no item focado) e **Vidro** (painéis translúcidos, estilo iOS). Nenhuma cor
está fixa no código da UI: tudo sai de `tema_atual()`, então acrescentar um tema é
acrescentar uma entrada na tabela.

### Controles

Não há tela sensível ao toque nesses aparelhos, então tudo é navegável pelo gamepad:

| Botão | Ação |
| --- | --- |
| **A** (`BTN_EAST`) / Enter | abre/confirma |
| **B** (`BTN_SOUTH`) / Esc | volta (na tela cheia, sai do vídeo; no menu, volta um nível; na Biblioteca com álbum aberto, volta para a lista de álbuns) |
| **X** (`BTN_NORTH`) / Espaço | tocar/pausar |
| **Y** (`BTN_WEST`) / F1 | menu (o mesmo do SELECT) |
| **SELECT** (`BTN_SELECT`) | abre o **menu de configurações** em qualquer aba |
| **L / R** (`BTN_TL`/`BTN_TR`) | troca de aba |
| **L1 / R1** (`BTN_TL2`/`BTN_TR2`) | música: faixa anterior/próxima; vídeo: −30 s/+30 s; grade: pula uma página |
| **D-pad / analógico esquerdo** | navegação (segurar repete) |
| **◀ ▶ na tela cheia / no menu** | −10 s / +10 s · muda o valor do item |
| **Volume do aparelho** (`gpio-keys-vol`) | volume, em qualquer tela — só volume |

### Botão de power

O power (`rk805 pwrkey`, `/dev/input/event0`) é o liga/desliga da interface:

| Gesto | O que acontece |
| --- | --- |
| toque curto | **apaga a tela** (DPMS off — é o que economiza bateria de verdade) |
| qualquer tecla com a tela apagada | acende de novo, sem perder o que estava tocando |
| 3 s segurando | abre o diálogo **Desligar?** (◀ ▶ escolhe, A confirma, B cancela) |
| sair do app com a tela apagada | o painel é religado antes de encerrar |

Para isso o logind **não** pode tratar a tecla: o drop-in
`/etc/systemd/logind.conf.d/10-ouvila-media-power.conf` põe
`HandlePowerKey=ignore` (o padrão do systemd desligaria o aparelho no toque, antes do
app ver a tecla).

O **brilho tem piso de 10%** (`WM_BRILHO_MIN`): abaixo disso o painel fica ilegível e
parece que o aparelho travou. Para economizar bateria existe o "apagar a tela", que põe
o painel em DPMS off — baixar o backlight sozinho economiza muito menos.

Os códigos são os do `rocknix-joypad` confirmados em
[hardware-e-drivers.md §3.2.1](hardware-e-drivers.md) — o mesmo mapa que o perfil
`Rocknix_Gamepad.cfg` do RetroArch usa.

### Onde fica a mídia

```text
/storage/media/music/    músicas (ext4 — Wi-Fi + scp)
/storage/media/videos/   vídeos
/boot/media/music/       músicas copiadas pelo PC (partição FAT, sem rede)
/boot/media/videos/      vídeos
/storage/sdcard1/media   cartão de dados no slot 1 (auto-montado)
/storage/sdcard2/media   cartão de dados no slot 2
```

A ordem vem de `media_roots` em `/boot/ouvila.conf` (separado por `:`). A varredura é
recursiva (até 4 níveis) e roda em segundo plano — a UI nunca espera o disco.

**Tags:** ID3v2 (`TIT2`/`TPE1`/`TALB`), ID3v1 e o bloco Vorbis comment do FLAC são
lidos direto do arquivo. Sem tag, `Artista - Título.ext` vira artista/título.

### Configuração (`/boot/ouvila.conf`)

| Chave | Para quê |
| --- | --- |
| `display_transform` | `auto` (pelo modelo) ou `normal`/`rotate-90`/... |
| `media_volume` | volume inicial (0..100) |
| `media_audio_out` | dispositivo ALSA: `default`, `hw:0,0` ou `-` (sem som) |
| `media_roots` | pastas de mídia, separadas por `:` |
| `media_exit` | `poweroff` (padrão) ou `stay` ao fechar a UI |

O brilho é ajustado no **menu (SELECT) → Brilho** com ◀ ▶, gravando em
`/sys/class/backlight/*/brightness` e respeitando o piso de 10%.

Esses cinco são **de boot** e continuam sendo editados à mão no cartão. O que o
**menu** muda vai para outro arquivo, `/boot/ouvila-media.conf`, que o próprio app
escreve (o `ouvila.conf` nunca é reescrito pelo app):

| Chave | Valores |
| --- | --- |
| `idioma` | `pt`, `en`, `zh`, `hi`, `es`, `ar`, `bn`, `id`, `ur`, `fr` |
| `tema` | `neon`, `oled`, `mono`, `mono-glow`, `vidro` |
| `modo` | `0` em ordem, `1` repetir álbum, `2` repetir faixa, `3` aleatória |
| `eq_preset` | `0`..`8` (8 = personalizado) |
| `eq0`..`eq9` | ganho de cada banda em dB, −12..+12 |
| `preamp` | pré-amplificação em dB, −12..+12 |
| `neural` | `0`/`1` (só tem efeito se houver NPU) |

### Testes sem o aparelho

```sh
build/media-player-sysroot.sh /tmp/wm-sysroot   # cabeçalhos + libs do bookworm arm64
WM_SYSROOT=/tmp/wm-sysroot build/media-player.sh --check   # compila contra eles
build/media-player.sh --native                  # binário para a máquina local
tools/test-media-player.sh                      # biblioteca, abas, decoder e launcher
```

O script resolve a **closura de dependências** do índice (169 pacotes no bookworm hoje):
os `.so` referenciam `libaom3`, `libva2`, `libssh-gcrypt-4`, `libspeex1`..., e sem elas o
link para em "referência não definida" mesmo o nosso código não usando essas funções.
Três detalhes que só aparecem em cross-compilação:

1. o **glibc do alvo não entra** no sysroot — os cabeçalhos dele vencem os do
   cross-compiler e quebram com `L_tmpnam` não declarado (o script baixa e não extrai);
2. os **ld scripts do glibc** (`libc.so` → `/lib/aarch64-linux-gnu/libc.so.6`) são
   removidos, porque os `-L` do sysroot vêm antes dos diretórios do toolchain;
3. o link usa `-Wl,-rpath-link` e também `<sysroot>/lib/aarch64-linux-gnu`, porque
   `dpkg-deb -x` não aplica o usrmerge (zlib/libselinux ficam em `/lib`).

O código usa `_DEFAULT_SOURCE`, não `_GNU_SOURCE`: no glibc >= 2.38 o `_GNU_SOURCE`
transforma `sscanf`/`strtol`/`fscanf` em `__isoc23_*`, que exigem **GLIBC_2.38** em
tempo de execução — e a imagem roda bookworm (2.36). O build confere com `objdump -T`
que a maior versão exigida é <= 2.36 e falha com a explicação se não for.

O `media-player-sysroot.sh` existe porque no Debian o arquivo `.so` é um *symlink* e o
alvo só está no pacote de **runtime**: baixar apenas os `-dev` resolve os cabeçalhos e
quebra no link (`cannot find -lavformat`). O script baixa os dois e confere cada `-l`
(symlink **e** alvo) antes de compilar — falha em segundos em vez de em 40 minutos.

Em cross-compilação o script **não** usa `pkg-config`: ele descreve o host e os
`-I/usr/include/<triple-do-host>` que devolve vencem os cabeçalhos internos do alvo
(glibc/asm). O sintoma engana — passam os cabeçalhos que o host também tem e falham só
os do sysroot.

Os caminhos de cabeçalho também não são óbvios: `libdrm-dev` põe `drm.h` em
`/usr/include/libdrm`, `libfreetype-dev` em `/usr/include/freetype2` e o FFmpeg do
Ubuntu em `/usr/include/<triple>/`. O script descobre os três (e usa `pkg-config`
quando existe) e **testa cada cabeçalho antes de compilar**, listando os pacotes que
faltam em vez de morrer num `fatal error` no meio de um arquivo.

O binário tem modos que existem só para isso:

```sh
ouvila-media --screenshot tela.ppm --tab videos      # desenha uma aba e grava
ouvila-media --screenshot menu.ppm --menu            # com o menu de configurações aberto
ouvila-media --screenshot z.ppm --idioma ar --tema vidro   # idioma e tema iniciais
ouvila-media --selftest                              # exercita a navegação
ouvila-media --scan                                  # lista a biblioteca
ouvila-media --play filme.mkv --frame-out q.ppm      # decodifica e grava um quadro
```

O `--selftest` é o que pega regressão de navegação: ele manda teclas sintéticas
(OK, L/R, setas, volume, B, power, SELECT, L1/R1) e confere o que a UI publica —
não substitui o teste no aparelho, mas evita descobrir no cartão que a lista parou de
responder. São **43 casos** e o script exige 40+ (um selftest que aborta cedo — biblioteca
vazia, por exemplo — não pode "passar").

A partir da v0.3 o `tools/test-media-player.sh` tem ainda o grupo **3b**: desenha a UI
nos **10 idiomas** e nos **5 temas** e exige que as telas resultantes sejam *diferentes*
entre si (um tema que não aplica, ou uma tradução que não carrega, cai aqui).

No CI, a variante roda `tools/test-media-player.sh` **antes** de compilar o kernel
(binário nativo, ~2 min) e, no passo do rootfs, executa o binário **arm64** com qemu
(`--version`) para provar que as bibliotecas resolvem.

### O que já foi validado no aparelho (e o que não foi)

**Confirmado na X35H (2026-11-01, imagem `media-player-v0.1`):** o app sobe pelo
`media-player.service`, o **DRM/KMS desenha em tela cheia** (painel 640x480,
`transform normal`), o **ALSA toca** (o usuário ouviu a música no aparelho) e a
navegação pelas abas responde. A biblioteca leu as faixas do cartão.

**Corrigido a partir desse teste** (é o que a próxima imagem leva):

1. **nada avançava ao fim da faixa** — o log mostrava "terminou (interrompido)" a cada
   1-2 s: o app fechava o player e ficava em silêncio. Agora, ao terminar uma música ele
   abre a próxima da lista; no vídeo, volta para a grade em vez de emendar outro.
2. **ligar/desligar estava na aba Sistema**, onde o botão de volume se confundia com o
   brilho. Agora: power curto apaga/acende a tela, 3 s abre a confirmação de desligar, e
   a aba Sistema virou **só informação** (configuração toda no menu de SELECT).
3. **brilho sem piso** — agora para em 10% e existe "apagar a tela" (DPMS off), que é o
   que economiza bateria.
4. **L1/R1 sem função** — agora saltam faixa (música), página (grade) e ±30 s (vídeo).

**Ainda não validado em hardware** (o teste acima foi antes destas mudanças):

1. o **power** de verdade: o drop-in do logind
   (`HandlePowerKey=ignore`) só existe na imagem nova; sem ele o systemd desliga o
   aparelho no toque curto.
2. **DPMS off/on** no painel da X55/X35H (o app usa a propriedade `DPMS` do conector).
3. **vídeo em tela cheia**: o RK3566 mainline não tem decode de vídeo por hardware, então
   720p é decodificado em software. 640x480/720x480 deve ir bem; 720p60 provavelmente
   não. Só o decodificador offline foi testado (quadro sai com conteúdo).
4. **tudo o que a v0.3 acrescentou** (menu com submenus, biblioteca por álbum, 10
   idiomas, 5 temas, equalizador, relógio, modos de reprodução): validado só offline —
   compila para arm64 contra os cabeçalhos do bookworm, 43 casos de selftest verdes, as
   4 abas + menu + 10 idiomas + 5 temas desenham, e o decodificador entrega quadro. O
   que **só o aparelho** pode dizer:
   - o árabe/hindi/chinês **aparecem ligados/corretos** na tela (o shaping foi medido
     fora do aparelho: "مرحبا" sai com 46 px ligado contra 84 px com as letras soltas);
   - o **equalizador muda o som** de forma audível (e não distorce);
   - o **Wi-Fi/BT pelo menu** acha redes/dispositivos na X55 (na X35H não há rádio: o
     SDIO não responde, então os dois itens aparecem como "Ausente");
   - o **relógio** aplica de verdade (o app roda como root e usa `clock_settime`;
     sem `CAP_SYS_TIME` ele falha em silêncio e o log registra o motivo).
4. **desempenho de música**: abrir a próxima faixa a cada fim recria o pipeline do
   FFmpeg — se aparecer um intervalo audível entre faixas, o próximo passo é manter um
   leitor pronto (gapless).
5. **Gamepad**: o mapa usa os códigos do DTS; os nomes dos eixos (`ABS_X`/`ABS_Y`) são
   os do `rocknix-joypad`, mas a zona morta foi escolhida por cálculo (0..1023, centro
   512), não medida.

Sempre que algo falhar, o motivo fica em **`/boot/media-player.log`** (é o primeiro
lugar a olhar).

## Onde ficam ROMs, saves e BIOS

```text
/boot/roms/<sistema>/        ROMs (partição FAT — lê-se no PC, ~90 MB livres)
/boot/roms/sdcard1/          cartão de DADOS no slot SD1 (auto-montado, quando o
/boot/roms/sdcard2/          sistema NÃO está naquele slot) — aparece no navegador
/storage/roms/               ROMs também aqui, via Wi-Fi+scp (ext4, 4 GB)
/storage/sdcard1, sdcard2    os mesmos cartões de dados (ponto de montagem real)
/storage/saves/              save RAM (.srm)
/storage/states/             savestates
/storage/screenshots/        capturas
/storage/bios/               BIOS (PS1 exige)
```

**Cartões de dados (1 eMMC + 2 slots SD).** O aparelho tem três mídias e o sistema pode
estar em qualquer uma delas (ver o mapa em
[hardware-e-drivers.md](hardware-e-drivers.md#52-os-três-armazenamentos-emmc--2-slots-sd--medido-em-2026-10-28)).
`ouvila-extra-storage.service` monta automaticamente as que **não** têm o sistema:

| Situação | Resultado |
| --- | --- |
| sistema no SD1 | SD2 vira `/boot/roms/sdcard2` |
| sistema no eMMC | SD1 → `/boot/roms/sdcard1` e SD2 → `/boot/roms/sdcard2` |
| sistema no SD2 | SD1 vira `/boot/roms/sdcard1` |

Regras de segurança do serviço: nunca monta partição com label `BOOT`/`ROOTFS` (é o
sistema), nunca religa o slot que tem o root, e desmonta sozinho quando o cartão sai.
Cartão **inserido depois do boot** é detectado (o slot é religado); não é preciso
reiniciar. Log em `/boot/extra-storage.log`.

**Por que ROMs em `/boot/roms` (FAT):** é o único lugar que se abre de um PC **sem
rede**. O menu do RetroArch já abre nessa pasta — e os cartões de dados aparecem dentro
dela (`sdcard1`/`sdcard2`), então não é preciso navegar pelo sistema de arquivos.

A imagem cria `/boot/roms/LEIA-ME.txt` com a organização por sistema e os limites.

**Limite:** a partição FAT tem 128 MB no total (kernel + dtbs ocupam parte). Dá para
alguns jogos de 8/16 bits, não para uma coleção de PS1. Para biblioteca grande use um
**cartão de dados** (o slot extra aceita vfat/exfat/ext4, sem limite prático) ou o Wi-Fi
com `scp` para `/storage/roms`. Saves e savestates ficam no ext4 (`/storage/...`), não na
FAT — assim não se perdem ao copiar ROMs.

Para desligar a detecção de cartão inserido depois do boot (economizar energia),
`extra_storage_hotplug=off` em `/boot/ouvila.conf`.

## Cores incluídos

O que o **arm64 do bookworm** realmente tem (conferido no índice da arquitetura, não
no site de pacotes — ver o aviso no fim desta seção):

| Sistema | Pacote | Arquivo do core |
| --- | --- | --- |
| NES | `libretro-nestopia` | `nestopia.libretro` |
| SNES | `libretro-bsnes-mercury-balanced` | `bsnes_mercury_balanced.libretro` |
| GB/GBC | `libretro-gambatte` | `gambatte.libretro` |
| GBA | `libretro-mgba` | `mgba.libretro` |
| PC Engine | `libretro-beetle-pce-fast` | `mednafen_pce_fast.libretro` |
| PlayStation 1 | `libretro-beetle-psx` | `mednafen_psx.libretro` |

**PlayStation 1 exige BIOS.** É o único core de PS1 disponível para arm64 e ele precisa
de `scph5500.bin`/`scph5501.bin`/`scph5502.bin` em `/storage/bios`. A BIOS não vem com o
projeto.

**Não existem para arm64** (o apt falha com "não é possível encontrar o pacote", embora
existam para amd64/armhf): `libretro-fceumm`, `libretro-snes9x`, `libretro-genesisplusgx`
e `libretro-pcsx-rearmed`. Ou seja: **não há core de Mega Drive/Genesis** no arm64 do
bookworm. Se precisar, compile o core (genesis_plus_gx) na mão e coloque em
`/storage/cores`.

Também disponíveis, mas não incluídos: `libretro-beetle-vb` (Virtual Boy),
`libretro-beetle-wswan` (WonderSwan) e `libretro-desmume` (NDS, pesado).

### Dois detalhes do Debian que quebram build/config

1. **Os cores não são `.so`.** Vêm como **`.libretro`**
   (`/usr/lib/aarch64-linux-gnu/libretro/mgba.libretro`). O RetroArch aceita as duas
   extensões, mas qualquer contagem/verificação que só olhe `*.so` acha zero.
2. **Não use `packages.debian.org/bookworm/arm64/<pacote>` para validar um nome:** ele
   responde **HTTP 200 mesmo quando o pacote não existe** para a arquitetura (é uma
   página de erro). A fonte da verdade é o índice:

   ```sh
   curl -s http://deb.debian.org/debian/dists/bookworm/main/binary-arm64/Packages.gz \
     | zcat | grep -c '^Package: <nome>$'
   ```

   Foi assim que apareceram os quatro cores inexistentes acima — depois de um build
   falhar por eles.
3. **`rootfs/root` é modo `0700`** (padrão do debootstrap). Escrever o
   `/root/.config/retroarch/retroarch.cfg` exige `sudo` — e **ler também**: um `grep`
   sem `sudo` no arquivo responde `Permission denied` porque não consegue nem
   atravessar o diretório. Se você for verificar o conteúdo depois de escrever, use
   `sudo grep`.

## A armadilha dos nomes de driver (RetroArch 1.14)

O RetroArch do Debian 12 é o **1.14**, e nessa versão os nomes são diferentes dos
tutoriais atuais. Conferido no código-fonte:

| O que se quer | No 1.14 | Em versões novas |
| --- | --- | --- |
| Driver de vídeo | `video_driver = "gl"` (ou `glcore`, `vulkan`, `drm`) | idem |
| Wayland | **`video_context_driver = "wayland"`** — não é video_driver | virou driver |
| KMS/DRM direto | `video_driver = "drm"` + `video_context_driver = "kms"` | virou `kmsdrm` |
| Trocar driver pela linha de comando | **não existe `--video-driver`**; use `--appendconfig` | existe `--video-driver` |

Por isso o `start-retroarch.sh` faz o fallback com um arquivo extra e
`--appendconfig`, e não com `--video-driver`. Se você copiar uma configuração da
internet e a tela ficar preta, este quadro é o primeiro lugar a olhar.

## Como o app é iniciado

Mesmo desenho das duas variantes, para o comportamento ser previsível:

1. lê `display_transform` de `/boot/ouvila.conf` (`auto` escolhe pelo modelo);
2. sobe o **Weston** (backend DRM, `kiosk-shell`) com `transform` na saída `DSI-1` —
   é essa camada que gira a imagem;
3. roda o app em Wayland — no RetroArch já com o **tamanho da saída girada** em
   `video_fullscreen_x/y` (ver "Se o RetroArch não abrir");
4. se o Weston falhar, cai para o caminho sem compositor (Kodi: `--windowing=gbm`;
   RetroArch: `video_driver=drm`), esperando 3 s para o DRM ser liberado;
5. o motivo de qualquer falha vai para a **tela** (tty1) e para
   `/boot/{kodi,retroarch}-start.log`.

A lógica de rotação e o start do Weston ficam em
`common/overlay/usr/local/share/ouvila/display.sh` — **fonte única** dessa parte. O
launcher do Kodi ainda tem a cópia inline dele (foi validado em hardware assim, e não
mexemos nele sem necessidade); ao tocar nele, migre para o helper.

## Fechar o app: no RetroArch, sair **desliga**

O menu do RetroArch 1.14 só oferece "Restart RetroArch" e "Quit RetroArch" — **não
existe item "Desligar"** (nem jeito de criar um sem mexer no fonte do RetroArch). E
"Quit" só encerra o processo: o aparelho continuaria ligado, com a tela sem interface,
porque o `tty1` pertence ao app e o `getty@tty1` é mascarado. Sem SSH/serial, não havia
como desligar.

Por isso o `start-retroarch.sh` trata a **saída normal como desligamento**:

| Situação | O que acontece |
| --- | --- |
| "Quit RetroArch" no menu (com `quit_press_twice`, dois toques) ou hotkey de sair | aparece "Desligando..." no `tty1`, faz `sync` e `systemctl poweroff --no-block` |
| crash, ou sessão com status 0 e menos de 10 s | **não** desliga: é falha, tenta o driver `drm`, e o serviço para após 3 tentativas com o motivo na tela |

**Validado no aparelho em 2026-10-28** (X35H): "Quit RetroArch" → tela "Desligando..." →
o aparelho desligou de verdade (o gadget USB desapareceu do PC, o que descarta queda de
rede). Os demais casos (crash, sessão curta, `stay`, conf ausente, valor inválido) foram
testados no próprio aparelho com `retroarch`/`systemctl` falsos.

O `sync` antes de desligar existe porque a partição FAT (ROMs) e o ext4 (saves) podem
ter escrita pendente — e o RetroArch grava o `retroarch.cfg` no exit
(`config_save_on_exit = "true"`).

Ligado/desligado por `retroarch_exit` em `/boot/ouvila.conf` (partição FAT, editável no
PC, sem regravar o cartão):

```ini
retroarch_exit=poweroff   # padrão: fechar o RetroArch desliga o aparelho
retroarch_exit=stay       # só sai do app (depurar por SSH/serial)
```

O Kodi **não** precisa disso: ele tem menu de energia próprio ("Desligar").

## Trocar de variante sem regravar

Não dá para trocar o "app da imagem" pelo cartão, porque os pacotes diferem. O que dá
para escolher no cartão é o **aparelho** (DTB) e a **rotação**. Para ter as duas
experiências, grave duas imagens em dois cartões (ou use dois cartões por aparelho).

## Se o RetroArch não abrir

1. Leia `/boot/retroarch-start.log` no PC (partição FAT): ele diz se o Weston subiu,
   qual driver foi tentado, em que tamanho e o status de saída.
2. Na tela (tty1) aparece a mesma informação de forma resumida.
3. Console serial (`ttyS2`, 1500000n8) tem autologin do root; o `tty1` pertence ao app.
4. O `diagnostico.txt` do cartão continua sendo gerado e traz DRM, áudio, entrada e
   rotação efetiva.

### Sintomas já vistos (os dois primeiros custaram uma sessão inteira)

| Sintoma no log | Causa | Correção |
| --- | --- | --- |
| `xdg_surface geometry (720 x 1280) is larger than the configured fullscreen state (1280 x 720)`, e o RetroArch "termina com status 0" | a saída foi girada pelo Weston, mas o app pediu a superfície no modo **nativo** do painel | o launcher passa `video_fullscreen_x/y` com o tamanho **já girado** (`ouvila_rotated_size`); nunca deixe em `"0"` |
| `[Autoconf]: retrogame_joypad (…) not configured` | o pacote Debian do RetroArch **não traz** perfil de joypad | perfil em `/root/.config/retroarch/autoconfig/udev/Rocknix_Gamepad.cfg` (instalado pelo build) |
| `RetroArch (wayland) terminou com status 0 apos 3s` seguido de "tentando video_driver=drm" | morte precoce, não saída do usuário | é o comportamento novo e desejado: abaixo de 10 s não conta como sucesso |
| "Quit RetroArch" fecha o app e **o aparelho continua ligado** (tela sem interface, só serial responde) | o menu do RetroArch não tem "Desligar"; o `tty1` é do app e o `getty` está mascarado | o launcher desliga na saída normal; log grava `Saida normal do usuario (...) : desligando o aparelho` |

Para inspecionar o RetroArch **sem tomar a tela** (útil por SSH, com o serviço
rodando), use os drivers nulos — é assim que o autoconfig foi diagnosticado. Nesse tipo
de depuração, use `retroarch_exit=stay` em `/boot/ouvila.conf` (ou
`OUVILA_CONF=<arquivo com stay>`) para o aparelho **não** desligar quando o teste
terminar:

```sh
cat > /tmp/probe.cfg <<'CFG'
video_driver = "null"
audio_driver = "null"
input_driver = "udev"
log_verbose = "true"
CFG
timeout 12 retroarch --config /root/.config/retroarch/retroarch.cfg \
    --appendconfig /tmp/probe.cfg --verbose 2>&1 | grep -iE "autoconf|joypad"
```
