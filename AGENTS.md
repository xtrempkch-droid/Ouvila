# AGENTS.md — direcionamentos e avisos para agentes de IA

Este arquivo existe para que agentes de IA (e pessoas) que trabalham neste projeto
**se comuniquem**: deixem direcionamentos, avisos e o resultado do que fizeram. Leia-o
antes de mexer no repositório e **acrescente uma entrada** (na seção
[Registro de agentes](#registro-de-agentes)) ao terminar um trabalho.

Idioma do repositório: **português**. Mantenha comentários, commits e docs em pt-BR.

---

## 0. ONDE PARAMOS (2026-10-25, com o adendo de 2026-10-30 no fim do arquivo) — leia antes de tudo

Detalhe completo em [`docs/status.md`](status.md) — seção "PONTO DE RETOMADA" no topo.
Resumo:

| O que | Estado |
| --- | --- |
| Variante **`kodi`** (multimídia) | **pronta e validada** no X55 e X35H — build #120 (~356 MB) |
| Variante **`retroarch`** (jogos) | **imagem #119 validada no X55 e no X35H**: tela girada, áudio no ALSA, **todos os botões e os 2 analógicos**, sessão estável (X35H: 81 s) |
| X35H: áudio, vídeo 640x480@58,5 Hz, botões | **confirmados em hardware** |
| X35H: USB | **falha no controlador**: `dwc3 ... failed to enable ep0out` (só no X35H). DTBs oficiais comparados e idênticos. Falta o teste do pendrive |
| X35H: Wi-Fi/BT | **não existem** nesta imagem: `mmc3: Failed to initialize a non-removable card` (SDIO do rádio não responde) |
| X55 | tudo o que a imagem entrega foi validado, incluindo áudio no jack e no alto-falante |

**Para gerar a imagem de jogos:** `git tag retroarch-vN && git push origin retroarch-vN`
(push na `main` gera `kodi`; a tag `retroarch-*` gera `retroarch`). Se falhar, o passo
**Preflight** diz em segundos qual pacote/arquivo está errado, sem compilar kernel.

**Nunca repita (custou build):**
1. Validar pacote por `packages.debian.org/<suite>/arm64/<pkg>` — responde **HTTP 200**
   com página de erro quando o pacote não existe para arm64. Use
   `http://deb.debian.org/debian/dists/bookworm/main/binary-arm64/Packages.gz`.
2. Ler ou escrever em `rootfs/root/...` sem `sudo`: o diretório é **0700** e o erro é
   `Permission denied` (foi o que derrubou o build #112 com tudo mais correto).
3. A marca de skip do CI é lida no **corpo** da mensagem, não só no título — e
   **escrever a marca para documentá-la também cancela o build**. Custa uma tag
   silenciosa a cada vez (aconteceu 3x nesta sessão). Para documentar, escreva
   "skip-ci" sem os colchetes, ou mande o commit de documentação separado.
4. `:` dentro de `name:` de um step no YAML — quebra o parse do workflow.
5. Em Wayland girado, deixar `video_fullscreen_x/y = "0"` no RetroArch: ele pede a
   superfície no modo **nativo** (720x1280) e o Weston mata o cliente
   (`xdg_surface geometry ... larger than the configured fullscreen state`), com
   **status 0** — parece "saiu normal" e a tela fica preta.
6. Contar com perfil de joystick do pacote Debian do RetroArch: `dpkg -L retroarch |
   grep autoconfig` é **vazio**. Sem `Rocknix_Gamepad.cfg` o log diz
   `[Autoconf]: retrogame_joypad (...) not configured` e **nada** responde.
7. Escrever autoconfig do RetroArch com código evdev: os números são a **posição** na
   enumeração do driver udev (tabela em `docs/hardware-e-drivers.md` §3.2.1).

---

## 1. Qual é o objetivo do projeto

Colocar o **Kodi** para funcionar em handhelds **RK3566**, começando pelo
**Powkiddy X55**, e também X35S e X35H. O sistema é: U-Boot + Linux mainline v6.12 +
Debian Bookworm + Weston/Wayland + Kodi, em imagem de cartão SD.

Leia primeiro a visão de estado do projeto: [`docs/status.md`](docs/status.md)
(é o ponto de retomada, com o que testar e o que devolver).

## 2. Mapa do repositório

```text
common/        fragmentos de kernel e pacotes compartilhados
devices/<placa>/
  device.conf        metadados da placa (image_dtb, display_transform, ...)
  dts/ uboot/        fontes e referências
variants/<variante>/
  packages/          manifesto de pacotes
  overlay/           serviços e scripts instalados no rootfs
build/
  build.sh           empacota <device> <variant> localmente
  create-image.sh    cria a imagem SD (precisa de root)
  configure-x55-dts.py   normaliza o nó rocknix-joypad no DTS da X55
.github/workflows/build.yml   pipeline completo (é ele que gera a imagem)
docs/              status.md, build.md, findings.md, roadmap.md, porting.md, architecture.md,
                   r35s-rk3326.md (placa RK3326 — fora do pipeline RK3566), hardware-e-drivers.md
tools/             utilitários auxiliares (joypad_from_dtb.py, panel_overlay_from_dts.py)
```

> **Placas RK3326 não entram neste pipeline.** `devices/game-console-r35s/` é
> **RK3326** (família PX30), não RK3566: outro DTS, outro U-Boot (`odroid-go2`) e
> outros blobs (`px30_bl31`/`px30_ddr`). Está preparada e **recusada de propósito**
> (`image_dtb=` vazio). Ver [`docs/r35s-rk3326.md`](docs/r35s-rk3326.md).

## 3. Estado atual (resumo)

| Item | Estado |
| --- | --- |
| Boot, U-Boot, painel (display liga), Debian | **confirmado em hardware** |
| X55: boot ativo com o DTB da X55 + `display_transform=rotate-90` | **implementado** (runs #87/#88) |
| X35S/X35H: usam o **mesmo** DTB (`rk3566-powkiddy-x35s.dts` faz `#include` do X55) | confirmado no código |
| `rocknix-joypad` (17 botões / 4 eixos) + mapa do Kodi | confirmado no X55 (teste anterior) |
| **X55: Kodi não inicia — a tela fica em loop** | **RESOLVIDO** (Weston 10 criava `wayland-1`; `--socket=wayland-0`) |
| X55: ancoragem USB + SSH (`root`/`root` em 10.55.0.1) | **confirmado** (PC precisa de IP estático 10.55.0.2/24) |
| X35H: ancoragem USB | **aberto** (`failed to enable ep0out`) |
| Sentido da rotação no X55 | **confirmado no aparelho: `rotate-90`** |
| Wi-Fi/BT RTL8821CS | **firmware validado ao vivo** (`wlan0`, `hci0`); painel web na :8088 |
| Áudio (`asoc-simple-card: parse error`) e vídeo VA-API | **aberto** |

Detalhes e histórico completo: [`docs/status.md`](docs/status.md) e
[`docs/findings.md`](docs/findings.md). Não trate **hipótese** como fato.

## 4. Bloqueios e armadilhas já resolvidos (não repita o erro)

### 4.1 X55: Kodi não iniciava (tela em loop) — RESOLVIDO

Sintoma: display ligava, terminal aparecia, mas o Kodi não abria (loop). Causas:

1. **`Restart=always` + `RestartSec=5`** transformava qualquer falha em loop e apagava
   a mensagem. Agora `Restart=on-failure` + `RestartSec=15` + `StartLimitBurst=3`.
2. **Weston 10.0.1 cria o socket como `wayland-1`** quando `WAYLAND_DISPLAY` está no
   ambiente. O launcher esperava `wayland-0`, dava timeout (30 s) e matava um Weston
   que estava funcionando. **Correção: `weston --socket=wayland-0` explícito.**
   Validado com reboot: `GUI format 720x1280`, `xdg_output` lógico 1280x720.
3. O fallback GBM falhava logo após matar o Weston (DRM ainda preso); agora espera 3 s.

Se voltar a falhar: o motivo fica em **`/boot/kodi-start.log`** e na **tela (tty1)**.

### 4.2 Ancoragem USB — X55 funciona; X35H **não ancora** (provável hardware)

- No **X55** a rede USB sobe (`g_ether`), o aparelho fica em **10.55.0.1** e o SSH
  (`root`/`root`) funciona. O PC **não** recebe IP por DHCP: configure **estático**
  `10.55.0.2/24` na interface USB.
- No **X35S/X35H**: **não funciona**, e a causa **não é o papel OTG**. Forçar
  `dr_mode = "peripheral"` não mudou nada. O que decide está no `dmesg`, e só o X35H
  tem esta linha:

  ```
  dwc3 fcc00000.usb: failed to enable ep0out
  ```

  Ou seja: o **gadget falha no CONTROLADOR** (`__dwc3_gadget_ep_enable` do `ep0out`,
  no Linux 6.12: `drivers/usb/dwc3/gadget.c`), antes de existir qualquer questão de
  cabo, VBUS ou papel. Consequência: o UDC nunca chega a `configured` (fica
  `not attached`) e `usb0` fica `NO-CARRIER`. O `rk817-charger/online` também é **0**
  no X35H contra **1** no X55, o que sugere que a porta pode nem estar ligada a esse
  controlador.
  - **Não repita:** "resolver com peripheral" (testado, falhou) e "conferir o DTB"
    (conferido: nós USB e PHY **byte a byte idênticos** ao DTB oficial da X55 —
    comparei os dois DTBs do release ROCKNIX 20261001).
  - **Teste que decide:** plugar um pendrive e ver se aparece algo em
    `/sys/bus/usb/devices/`. Se aparecer, a porta é **host** e um adaptador
    USB-Ethernet resolve rede/SSH; se não aparecer nada em nenhum dos dois papéis, a
    porta é só de carga. Use o **mesmo cabo que funciona na X55** como controle.
  - `usb-watch.log` agora grava o `dmesg` de USB **sempre na foto inicial** (antes só
    gravava "na mudança", e no X35H nunca há mudança — foi por isso que o `ep0out`
    demorou a aparecer).
  - **Teste que decide:** com o console serial aberto, `dmesg -w` e plugar um pendrive;
    depois `ls /sys/bus/usb/devices/`. Antes de concluir "sem dados", repetir com o
    **mesmo cabo que funciona na X55**.
  - A imagem traz `walkmam-usb-watch.service` → `/boot/usb-watch.log` com o retrato do
    estado USB a cada mudança (UDC, `usb_role`, dispositivos, `online`, `dmesg`).
  - Continuam disponíveis `extlinux.conf.x35s` (otg, padrão) e
    `extlinux.conf.x35s-peripheral` (teste).
- O gadget não faz DHCP — se o PC ficar em 169.254.x.x, é isso.

### 4.2.0 tty1 é do Kodi — não habilite getty ali

`kodi.service` usa `TTYPath=/dev/tty1`, `StandardInput=tty` e
`Conflicts=getty@tty1.service`. Se o `getty@tty1` também estiver habilitado (com ou sem
autologin), os dois disputam a unidade no boot e **um dos dois recebe SIGTERM**: no
`/boot/kodi-start.log` aparece `Kodi (wayland) terminou com status 143`, depois a
tentativa GBM falha (`ERROR: Unable to create GUI`, 255) e o serviço entra em ciclo.

A imagem agora **mascara** `getty@tty1.service` (`systemctl mask`). O shell de
emergência fica no **console serial** (`ttyS2`, autologin do `root`). Não recrie o
drop-in de autologin em tty1.

### 4.2.1 Áudio: mux tem que ficar em `HP` (não "conserte" isso)

No X55 o amplificador do alto-falante (`spk_amp`) é alimentado por **`HPOL`/`HPOR`**, não
pelo `SPKO` do codec. Por isso o `Playback Mux` **deve** ficar em `HP`; em `SPK` o
aparelho fica **mudo**. Áudio validado pelo usuário nos **dois** caminhos (jack 3,5 mm e
alto-falante interno), com `Headphones Jack` alternando por detecção de GPIO
(`gpio4 RK_PC6`).

### 4.3 Bluetooth do X55 não inicializava — RESOLVIDO (2026-10-24)

Sintoma: `hci0` existe, mas com `BD Address 00:00:00:00:00:00`; `bluetoothctl show`
responde "No default controller available"; `dmesg` mostra
`RTL: no UART config entry found`.

Causa: o BT é UART no `uart1` com `compatible = "realtek,rtl8821cs-bt",
"realtek,rtl8723bs-bt"`. Não existe driver para `rtl8821cs-bt`; casa o **`hci_h5`**
(three-wire) pelo fallback `rtl8723bs-bt`. O `h5_btrtl_setup()` chama
`btrtl_get_uart_settings()`, que **exige** no `rtl8821cs_config.bin` uma entrada de
offset **0x0c** (baudrate + flow control). O `linux-firmware` **não publica** esse
arquivo; o `rtl8821c_config.bin` (10 bytes) renomeado não tem a entrada → `-ENOENT` →
o setup aborta antes de baixar o firmware.

Fix (no CI): **gerar** o `rtl_bt/rtl8821cs_config.bin` com 25 bytes — assinatura
`0x8723ab55`, `total_len=0x13`, uma entrada `0x0c` de 16 bytes com baudrate
`0x04928002` (1500000) e `data[12]=0x5f` (bit 2 = flow control, casa com o
`uart-has-rtscts` do `uart1`). Validado: `RTL: fw version 0x75b8f098`, MAC real,
`UP RUNNING`, scan listando dispositivos e **sobe sozinho depois de reboot**.

Não troque esse arquivo por um renomeado do 8821C: volta a quebrar.

### 4.4 Variantes: `kodi` (multimídia), `retroarch` (jogos) e `media-player`

O mesmo kernel/DTB gera **três imagens**. O que muda é o rootfs e o serviço do `tty1`.
A variante é escolhida no `workflow_dispatch` (input `variant`); **push na `main` constrói
`kodi`**, que é o que se usa para testar no aparelho.

* `variants/kodi/` — Kodi + Weston + add-on de rádio.
* `variants/retroarch/` — RetroArch + cores + Weston, sem add-on (rádio pelo painel web).
* `variants/media-player/` — **um binário em C** (`src/`, compilado por
  `build/media-player.sh`): DRM/KMS + FFmpeg + ALSA + FreeType + evdev. **Não usa
  Weston, WebKit/cog, mpv nem backend HTTP** — foi um pedido explícito do usuário
  ("nada de usar outra camada já existente"). Abas: Música, **Vídeos** (tela cheia),
  Rádio e Sistema. Nada disso foi testado em hardware ainda.

Detalhes em [docs/variantes.md](docs/variantes.md). Pontos que custam tempo se esquecidos:

1. **Nomes de driver do RetroArch 1.14** (o do bookworm): `video_driver = "gl"` +
   **`video_context_driver = "wayland"`** (Wayland é contexto, não driver); KMS/DRM é
   `video_driver = "drm"` + `video_context_driver = "kms"` (no 1.17+ virou `kmsdrm`).
   E **não existe `--video-driver`** nessa versão: para trocar driver use
   `--appendconfig <arquivo>`.
2. O build **descobre** o diretório dos cores (`/usr/lib/<triple>/libretro`) em vez de
   chutar, e falha se houver menos de 5 cores ou se o `libretro_directory` não tiver
   sido substituído no `retroarch.cfg`.
3. A rotação/Weston saiu para `common/overlay/usr/local/share/walkmam/display.sh`
   (fonte única). **O launcher do Kodi ainda tem a cópia inline** — foi validado em
   hardware assim; ao mexer nele, migre para o helper.
4. `dump-diagnostico.sh`, `radio-retry.sh`, `usb-watch.sh` e o painel web ainda moram em
   `variants/kodi/overlay/` embora sejam compartilhados. Funciona, mas o lugar certo é
   `common/overlay/`; mover exige atualizar todos os `install` do workflow.
5. N64 (`mupen64plus-next`) e PSP (`ppsspp`) existem para arm64 mas **não** entram por
   padrão: desempenho não medido no RK3566.

## 5. Como construir

**Nuvem (fluxo completo):** Actions → *Build Mainline RK3566 (Powkiddy X55) OS* →
Run workflow (ou um push no `main`). Gera o artifact
`rk3566-x55-mainline-sdcard-image`.

**Releases:** o fim do job publica a imagem no GitHub Releases (o artifact expira em 90
dias e exige login; o release não). Regras:

| Ref | Release |
| --- | --- |
| tag `v*` | release com o nome da tag (variante `kodi`) |
| tag `retroarch-*` | release com o nome da tag (variante `retroarch`) |
| push na `main` | release rolante **`latest`** (`--prerelease`), assets sobrescritos |

Os passos usam `gh release ... --clobber` e o token do próprio workflow, por isso o job
declara `permissions: contents: write`. O checksum é **por arquivo**
(`<imagem>.img.gz.sha256`), não um `SHA256SUMS` único, para as duas variantes conviverem
no `latest` sem uma apagar o checksum da outra. A imagem continua sendo `.img.gz` de
cartão SD — **não** é ISO9660 (o U-Boot fica nos setores 64 e 16384). Um release só é
criado se o build chegar ao fim; tag engolida pela marca de skip-ci não gera release.

**Local (U-Boot + kernel + DTB, sem root):** possível, com toolchain extra. O CI é o
caminho oficial; o rootfs Debian e a montagem da imagem exigem `sudo`.

**Cuidado com `[skip ci]` (custou cinco builds nesta sessão):** o GitHub olha a
mensagem do **commit que o ref aponta** — e **corpo também conta**, não só o título.

1. Push na branch: vale o commit de topo. Se você commitar uma correção e o último
   commit do push for `docs: ... [skip ci]`, o workflow **não roda** e a correção fica
   sem imagem.
2. **Push de tag:** vale a mensagem do commit **apontado pela tag**. Uma tag criada em
   cima de um commit com `[skip ci]` também não roda — inclusive a tag
   `retroarch-*`, que é justamente o gatilho da variante de jogos.
3. **Documentar a armadilha também dispara a armadilha.** Um commit cujo texto
   explique `[skip ci]` contém a marca e por isso é ignorado — foi assim que a tag
   `retroarch-v0.7` e depois a `v0.8` não geraram build. Escreva "skip-ci" sem os
   colchetes ao documentar.

Saídas: dois pushes separados; `git commit --allow-empty` sem a marca para gerar a
imagem; ou marcar a tag num commit limpo. Um `git push --delete` da tag e recriar em
outro commit resolve sem force-push na branch. Para conferir se o build nasceu, olhe
`/actions/runs?branch=<tag>`: se voltar vazio, o push foi engolido pela marca.

## 5.1 Acesso ao aparelho (para depurar)

- Console **serial** (`ttyS2`, 1500000n8) e **tty1** com autologin do `root`.
- **SSH pela rede USB** (`g_ether`): o aparelho fica em **10.55.0.1** (o PC usa
  `10.55.0.2/24`), usuário `root`, senha `root`.
  - Se o login por senha for **recusado**, quase sempre é o `ssh_config` do Debian:
    o padrão é `PermitRootLogin prohibit-password`. A imagem instala
    `/etc/ssh/sshd_config.d/10-walkmam.conf` com `PermitRootLogin yes` e
    `PasswordAuthentication yes`.
- **Logs no cartão** (partição FAT, leitura no PC): `diagnostico.txt`,
  `kodi-start.log`, `usb-gadget.log`, `weston.log`.

## 6. Regras e armadilhas (leia antes de editar)

1. **Não inferir GPIO/pinagem.** Nunca invente valores de hardware; use fonte
   confirmada ou marque como hipótese.
2. **Um DTB por placa, mas X35S e X35H compartilham o mesmo.** O `rk3566-powkiddy-x35s.dts`
   faz `#include` do DTS da X55 e sobrescreve o painel (640x480,
   `rocknix,generic-dsi`). Bootar a X55 com o DTB da X35S deixa a tela preta — foi a
   regressão corrigida nos runs #87/#88.
3. **FDT ausente = aparelho sem imagem** (LED aceso). O step de imagem falha de
   propósito se faltar o DTB ativo.
4. **Rotação vem de `display_transform`** em `devices/<placa>/device.conf`, gravada em
   `walkmam.conf` na partição FAT e lida por
   `variants/kodi/overlay/usr/local/bin/start-kodi-wayland.sh`. Não fixe o transform no
   `weston.ini`.
5. **Aspas dentro de `bash -c '...'` no `build.yml` já quebraram o build (#82/#83).**
   Prefira instalar arquivos do overlay com `install` em vez de heredoc inline.
6. **Não commitar segredos** (tokens, senhas). A senha do `root` da imagem é `root` e
   o SSH está ligado — ver "Segurança" no roadmap.
7. Fixe versões de fontes externas por commit quando possível (hoje o CI clona
   `ROCKNIX/distribution` e o `rkbin` na ponta; os blobs citados dos `rkbin`
   `rk3568_bl31_v1.43` / `rk3566_ddr_1056MHz_v1.18` já não existem e o fallback entra).

## 7. Protocolo de comunicação entre agentes

- Toda mudança relevante deve atualizar [`docs/status.md`](docs/status.md) (bloqueios,
  "confirmado x hipótese", runs do CI) e, se for decisão de engenharia, `docs/findings.md`.
- Ao terminar, **acrescente uma entrada** em [Registro de agentes](#registro-de-agentes)
  (mais recente por último), dizendo: data, autor (o modelo/agente), o que fez, o que
  ficou pendente e o que o **próximo** precisa testar.
- Deixe o repositório sempre num estado que **compila** (o `main` dispara build).

---

## Registro de agentes

<!-- Mais recente por último. Modelo:

### AAAA-MM-DD — <autor/agente>
- Fez: ...
- Pendente / próximo: ...
-->

### 2026-10-09 — Copilot (sessão de correção do X55)

- Fez: corrigiu a regressão de "sem imagem" (o boot ativo usava o DTB da X35S; agora
  usa o DTB da X55, com o DTB da X35S/X35H em `extlinux/extlinux.conf.x35s`); criou
  `display_transform` por `device.conf` (X55 `rotate-90`, X35S/X35H `normal`); o
  `build.sh`/`create-image.sh` gravam `walkmam.conf`. Submeteu ao `main` e o build
  **#87** passou.
- Fez também: melhorou o diagnóstico de USB/Kodi (role/UDC, `kodi-start.log`,
  `dump-diagnostico` em 2 capturas) e criou uma variante de boot
  `extlinux.conf.x55-peripheral` (USB forçado a *device*) para teste. Criou este
  `AGENTS.md`. Build **#88** passou.
- Fez ainda: **corrigiu o SSH** — o Debian 12 bloqueia login do root por senha
  (`PermitRootLogin prohibit-password`), então `root:root` era recusado; a imagem agora
  instala `/etc/ssh/sshd_config.d/10-walkmam.conf` com `PermitRootLogin yes` +
  `PasswordAuthentication yes`. Launcher do Kodi com fallback **Weston → GBM** e o
  motivo do erro **na tela**; `kodi.service` com `Restart=on-failure`/`RestartSec=15`
  para a falha não virar loop que apaga a mensagem. Push `63f4bd7` → **build #89**.
- **Handoff (2026-10-09):** o usuário **vai criar/baixar a imagem (#89) quando voltar
  e avisar**; eu sigo com o teste. Confirmar que o #89 ficou `success`.
- **RESOLVIDO em 2026-10-23 (com SSH ao vivo no X55):** o Kodi não abria porque o
  **Weston 10.0.1 criava o socket `wayland-1`** (com `WAYLAND_DISPLAY` no ambiente) e o
  launcher esperava `wayland-0`, matando o Weston após 30 s. Correção:
  `weston --socket=wayland-0` explícito + 3 s antes do fallback GBM. Confirmedo com
  **reboot**: `kodi.service` ativo, Kodi em Wayland, `GUI format 720x1280`, `xdg_output`
  lógico 1280x720 (`rotate-90` validado no aparelho). Também validado o **SSH root/root** e a rede USB
  (PC precisa de IP estático `10.55.0.2/24`; o gadget não faz DHCP).
- **Áudio (2026-10-23/24):** o `sound` ficava em deferred probe porque o **I2S do
  Rockchip** não estava no kernel (`# CONFIG_SND_SOC_ROCKCHIP`); o config da ROCKNIX
  pedia o filho, mas o `olddefconfig` descartava. Habilitados
  `SND_SOC_ROCKCHIP{,_I2S,_I2S_TDM}` + `&hdmi_sound` desligado junto com o HDMI.
  **Build #92 verde** (352 MB) — instalar e testar o áudio no aparelho.
- **Wi-Fi/Bluetooth (2026-10-24):** faltavam firmware (`rtw88/rtw8821c_fw.bin`,
  `rtl_bt/rtl8821cs_fw.bin`+config), `wpa_supplicant` (sem ele o NM deixa `wlan0`
  `unavailable`) e BlueZ. Validado ao vivo após copiar os firmwares: `wlan0` (fw
  24.11.0) e `hci0` sobem. A imagem agora baixa os firmwares (tag 20260309, sha256) e
  instala `wpasupplicant iw wireless-tools rfkill bluez busybox`, além do **painel web
  na porta 8088** (`walkmam-webui.service`) para gerenciar Wi-Fi/BT.
- **Drift de fontes externas (2026-10-24):** os runs #90/#91 falharam porque o master da
  ROCKNIX mudou (kernel 7.2, patch 0012 removido, 0016 usando `&gpio_keys_control`).
  Agora U-Boot/ROCKNIX/rkbin estão **fixados por commit** e há guarda para `.rej`.
- **Próximos itens abertos:** validar o **áudio** no #92, VA-API
  (`rockchip_drv_video.so` ausente) e o **sentido visual** da rotação
  (`rotate-90` validado).

### 2026-10-24 — Copilot (Bluetooth do X55)

- Fez: fechou a causa raiz do Bluetooth (config UART `0x0c` ausente no
  `rtl8821cs_config.bin`). O CI passou a **gerar** o arquivo (25 bytes) em vez de
  renomear o do 8821C; conferido por sha256 no build. Validado ao vivo no aparelho com
  build #97 (trocando o arquivo e recarregando o `hci_uart`): firmware baixa
  (`fw version 0x75b8f098`), `hci0` com MAC real, `UP RUNNING`, scan acha dispositivos,
  e depois de **reboot** sobe sozinho. Wi-Fi também confirmado no boot sem retry.
- Pendente / próximo: gravar a imagem nova (#98) e validar áudio no aparelho; o
  esticamento horizontal de ~1,6% é do painel (67x121 mm vs modo 720x1280).

### 2026-10-24 — Copilot (add-on de rádio dentro do Kodi + créditos)

- Fez: criou o add-on **`script.walkmam.radio`** ("Walkmam Radio") porque o Kodi 20 no
  Linux não tem UI de rádio (o gerenciador de Wi-Fi só existe nos builds
  Windows/Android). É um script add-on (`xbmc.python` 3.0.0) em
  `variants/kodi/overlay/usr/share/kodi/addons/script.walkmam.radio/` que chama
  `nmcli` (listar/conectar/desconectar Wi-Fi) e `bluetoothctl` (ligar, procurar,
  parear, conectar, ficar visível). O CI instala e roda `py_compile` no `default.py`.
  Também adicionou ao `README.md` a seção **"Créditos e transparência sobre o uso de
  IA"** (DeepSeek como modelo criador dos códigos + Copilot como assistente, o que foi
  verificado em hardware e o que não foi).
- Validado no #97: o Kodi registra `CAddonMgr::FindAddons: script.walkmam.radio v1.0.0
  installed`; e a lógica rodou no aparelho com os binários reais — `wifi_redes()`
  devolveu SSIDs com sinal/segurança, `bt_info()` devolveu o controlador
  `A8:4F:A4:C9:98:4C`, `bt_escanear(8)` achou 2 dispositivos em 8,3 s.
- Armadilha: `bluetoothctl` imprime `[NEW] Device <MAC> <nome>` no scan e
  `Device <MAC> <nome>` em `devices` — o regex **não** pode ser ancorado no começo da
  linha; e linhas `[CHG] ... RSSI:` não trazem nome (usar só as `[NEW]`).
- Pendente / próximo: gravar a imagem (#99 em diante) e abrir o add-on pela TV para
  confirmar os diálogos (a lógica já foi testada, a GUI não).

### 2026-10-24 — Copilot (áudio confirmado + doc de portabilidade)

- Fez: registrou a confirmação do **áudio nos dois caminhos** (jack 3,5 mm e alto-falante
  interno) e criou **`docs/hardware-e-drivers.md`**, a referência de portabilidade: para
  cada um dos três aparelhos, a arquitetura de hardware e de driver (painel, backlight,
  entrada, áudio, Wi-Fi/BT, USB, energia), os números exatos (GPIO, SARADC, timings,
  trilhos, firmware), a lista de configs de kernel obrigatórias e **11 receitas prontas
  (F1–F11)** para portar a outro sistema, além da tabela sintoma → causa → correção.
- Também deixou explícito o que **não** é mainline e precisa ser carregado junto:
  `rocknix-joypad`, o shim `input-polldev`, `panel-generic-dsi.c` (painel da X35S/X35H),
  o DTS da X35S e o `rtl8821cs_config.bin` gerado.
- Corrigido de passo: `CONFIG_BT_HCIUART` é `=m` (não `=y`); na X35S o Wi-Fi/BT está
  **não medido** (só a X35H tem "sem WiFi/BT" registrado); o `dr_mode` da X55 vem do
  `rk356x.dtsi` como `otg` (não precisa de variante "peripheral").
- Pendente / próximo: validar a imagem #100 no aparelho (Bluetooth + add-on com o
  teclado corrigido) e confirmar as builds.

### 2026-10-24 — Copilot (imagem #100 validada no CI)

- Fez: **build #100 success** (356 MB), a imagem para usar: junta o Bluetooth corrigido
  (config UART do 8821CS gerada no build) e o add-on `script.walkmam.radio` com o
  teclado da senha corrigido. Conferido localmente que o bloco do CI gera o
  `rtl8821cs_config.bin` **byte-idêntico** ao validado no aparelho (sha256
  `6ddeb15f...`) e que a instalação do add-on produz os dois arquivos certos
  (`addon.xml` + `default.py`, registrado como `xbmc.python.script`).
- Pendente / próximo: usuário gravar o #100 e testar Bluetooth pelo add-on e o teclado
  da senha na TV; depois confirmar o resto (áudio já confirmado nos dois caminhos).

### 2026-10-24 — Copilot (X35H validada em hardware + correções de USB e rotação)

- Fez: registrou o **primeiro teste de X35H em hardware**: vídeo 640x480 a **58,5 Hz**
  em tela cheia, **áudio** e **botões/direcionais** funcionando. Só o **USB** ficou em
  aberto.
- **USB:** `failed to enable ep0out` é o DWC3 recusando o DEPCMD de ep0out porque o
  controlador está em modo *host* (`dr_mode = "otg"` do `rk356x.dtsi`, e nessas placas a
  detecção não resolve para *device*). O build agora gera `dtbs/<dtb>-peripheral.dtb`
  com `dr_mode = "peripheral"` num **helper reutilizável** que **verifica o DTB final**
  (não gera variante quebrada em silêncio) e a entrada `extlinux.conf.x35s` **já aponta
  para ela**; `extlinux.conf.x35s-otg` volta ao original.
- **Rotação (bug real):** o `walkmam.conf` saía sempre com `display_transform=rotate-90`,
  obrigando a editar à mão para X35S/X35H. Agora o padrão é `auto` e o launcher escolhe
  pelo `/proc/device-tree/model` (`*x55*` → `rotate-90`, resto → `normal`); valor
  explícito continua vencendo. Testado com matriz de casos (auto/vazio/explícito).
- **75 Hz (esclarecido):** não é limitação de hardware. O `panel-generic-dsi` só marca
  como preferido o modo `M` com `default=1` (o de 58,5 fps), e o Kodi usa o preferido do
  conector. Os 75,47 fps existem na lista mas marcados `(CAN FAIL)`. Para testar de
  verdade é preciso gerar um DTB com esse modo como `default=1`.
- Diagnóstico: `diagnostico.txt` agora traz o modelo do device tree, os modos DRM (o
  primeiro é o preferido) e a rotação efetiva — responde "o 75 Hz aplicou?" e "qual DTB
  está ativo".
- Pendente / próximo: gerar a imagem e o usuário testar o SSH por USB na X35H; se o
  `peripheral` não resolver, olhar o `diagnostico.txt` (campos `dr_mode` e `usb_role`).

### 2026-10-24 (2) — Copilot (diagnóstico da X35H: USB descartado como software)

- Recebi o `diagnostico.txt` da X35H (boot com
  `rk3566-powkiddy-x35s-rocknix-peripheral.dtb`). Conclusões com evidência:
  - `dr_mode` no device tree em uso = **`peripheral`** (confirmado no log);
  - `/sys/class/udc/fcc00000.usb` **existe**, mas `state = not attached`;
  - `usb0` sobe com `10.55.0.1/24` e fica `NO-CARRIER`;
  - `failed to enable ep0out` continua.
- Desmontei o `.dtb` que o aparelho realmente usou: `usb@fcc00000` com
  `dr_mode="peripheral"`, `phys`/`phy-names`, `extcon`, `maximum-speed` e o
  `usb2phy@fe8a0000`/`otg-port` todos `okay` — **estruturalmente igual ao da X55**, que
  funciona. Portanto o DTB não é o culpado.
- Conclusão honesta: **não é o papel OTG** (forçá-lo a device não mudou nada). A
  hipótese que sobrou é hardware: a porta USB-C da X35H provavelmente está no
  `usb@fd000000` (`dr_mode = "host"`), o que a torna **host-only** — nesse caso pendrive,
  teclado e **dongle USB-Ethernet funcionam**, mas gadget/SSH direto é impossível.
- Revertido o padrão `peripheral` (seria trocar o padrão por uma hipótese já testada e
  falha) e mantida a variante como teste. Também reverte o `dr_mode` só na X35S/X35H.
- Criado `walkmam-usb-watch.service` + `usb-watch.sh` (5 min no boot, log por boot em
  `/boot/usb-watch.log`, retrato a cada mudança + `dmesg` do instante) para o teste do
  pendrive produzir evidência.
- Pendente / próximo: o usuário plugar um pendrive e devolver `usb-watch.log`. Isso
  define se o SSH na X35H será por dongle USB-Ethernet ou se a porta não tem dados.

### 2026-10-24 (3) — Copilot (variante `retroarch`)

- Fez: criou a variante **`retroarch`** (versão de jogos) ao lado da `kodi` (multimídia).
  O workflow ganhou o input `variant` (push na main continua `kodi`), os pacotes saem de
  `variants/retroarch/packages/retroarch.list`, o build descobre o diretório real dos
  cores e gera o `retroarch.cfg` a partir de um modelo (com asserção), cria
  `/storage/{roms,saves,states,screenshots,bios}` e habilita `retroarch.service` no tty1.
  Artefato próprio: `rk3566-x55-retroarch-sdcard-image` (o nome do kodi não mudou).
- Extraiu a rotação/start do Weston para `common/overlay/.../display.sh` (fonte única) e
  o novo launcher a consome; o launcher do Kodi ficou intocado de propósito.
- Conferido no fonte do RetroArch 1.14 (não por suposição): wayland é
  `video_context_driver`, o driver KMS/DRM se chama `drm` (não `kmsdrm`) e **não existe**
  `--video-driver` nessa versão. Tudo isso está documentado e comentado no código, porque
  é o tipo de erro que só aparece na tela preta.
- `dump-diagnostico.sh` passou a registrar `retroarch-start.log`, a config efetiva, os
  cores instalados e o `usb-watch.log`.
- Pendente / próximo: **nada disso foi testado em hardware**; o primeiro teste precisa
  confirmar boot, rotação e áudio no RetroArch, e se o joypad ROCKNIX é reconhecido.

### 2026-10-24 (4) — Copilot (variante retroarch: 4 builds, 4 bugs reais)

- Fez: criou a variante **`retroarch`** (jogos) ao lado de `kodi` (multimídia), com
  input `variant` no workflow e gatilho por tag (`retroarch-*`). Extraiu a rotação/start
  do Weston para `common/overlay/.../display.sh` (fonte única) e deixou `/roms` na
  partição FAT para copiar jogo pelo PC — essencial na X35H, onde o USB não ancora.
- **4 builds da variante falharam, cada um por um motivo diferente, todos corrigidos:**
  1. `libretro-fceumm`, `libretro-snes9x`, `libretro-genesisplusgx` e
     `libretro-pcsx-rearmed` **não existem para arm64** (existem para amd64/armhf).
     Descoberto simulando `apt-get -s install` com `APT::Architecture=arm64` e o índice
     `binary-arm64/Packages.gz`. A lista passou a usar o que existe: `nestopia`,
     `bsnes-mercury-balanced`, `gambatte`, `mgba`, `beetle-pce-fast`, `beetle-psx`.
  2. Os cores vêm como **`.libretro`** (e também `_libretro.so`), não só `.so`: a
     contagem por `*.so` dava números estranhos.
  3. A asserção do `retroarch.cfg` comparava `.../libretro/` (gerado) com
     `.../libretro` (esperado) → abortava por causa da própria checagem.
  4. **`rootfs/root` é 0700**: o `grep` de verificação rodava sem `sudo` e recebia
     `Permission denied`. O log do #112 provou que os 24 pacotes e **12 cores** já
     estavam instalados — só a verificação falhava.
- Também: `concurrency` (um build por ref, cancela o anterior — cheguei a 4 runs em
  paralelo), passo de **Preflight** que valida nome de variante, manifesto, cada pacote
  contra o índice arm64 e os arquivos de overlay **em segundos**, e correção de um erro
  de YAML meu (`:` no `name:`).
- Testado sem hardware: launcher do RetroArch com binários falsos — rotação por modelo
  (X55 → `rotate-90`, X35S → `normal`), valor explícito vencendo o modelo, fallback
  wayland → drm via `--appendconfig`, falha total saindo 1 e binário ausente com
  mensagem no log.
- **Pendente / próximo:** o build **#116** (`retroarch-v0.6`) não terminou; ver se deu
  `success` e, em caso positivo, testar no aparelho (boot, rotação, áudio, joypad e
  cores no menu). Se falhar, o Preflight diz o motivo sem compilar. No X35H falta o
  teste do pendrive para decidir se a porta é host-only.


### 2026-10-25 (1) — Copilot (retroarch no hardware: 2 bugs; X35H: USB e rádio)

- **A variante de jogos nunca tinha rodado.** Ao rodar, dois bugs reais:
  1. **Tela preta com serviço "success":** `xdg_surface geometry (720 x 1280) is
     larger than the configured fullscreen state (1280 x 720)`. O Weston gira a saída,
     mas o RetroArch dimensiona a **própria superfície** com `video_fullscreen_x/y`;
     com `"0"` ele usa o modo **nativo** do painel e pede superfície maior que o
     fullscreen. O Weston mata o cliente e o RetroArch sai com **status 0** — parecia
     saída normal do usuário. Correção: `walkmam_rotated_size()` em `display.sh`
     (modo do DRM + transform) passado por `--appendconfig`; medido: 7,5 s → estável.
     Junto: sessão com status 0 e **menos de 10 s** deixou de contar como sucesso.
  2. **Nenhum botão respondia:** `[Autoconf]: retrogame_joypad (18507/4353) not
     configured`. O pacote Debian do RetroArch 1.14 **não traz perfil nenhum**
     (`dpkg -L retroarch | grep autoconfig` = vazio) e o projeto upstream de
     autoconfigs não tem perfil para `retrogame_joypad`. Criei `Rocknix_Gamepad.cfg`.
     **A armadilha:** os números não são códigos evdev, são a **posição** na
     enumeração do driver udev — calculei do bitmap real
     (`/sys/class/input/event3/device/capabilities/key`). Depois do perfil: 17 botões,
     D-pad, L/R/L2/R2, L3/R3, F e os 2 analógicos corretos (teste do usuário).
- Diagnosticado **sem tomar a tela** (por SSH, com o serviço rodando): `video_driver =
  "null"`, `audio_driver = "null"`, `input_driver = "udev"` e `--verbose`.
- **X35H — USB:** a linha `dwc3 fcc00000.usb: failed to enable ep0out` existe **só** no
  X35H. Vem de `__dwc3_gadget_ep_enable()` dentro de `dwc3_gadget_start()`: o gadget
  aborta **antes** de negociar com o host, então papel/cabo/`dr_mode` não resolvem.
  Para encerrar a dúvida de device tree, baixei o release **ROCKNIX 20261001** e
  comparei os DTBs **oficiais** das duas máquinas: `usb@fcc00000`, `usb@fd000000` e
  `usb2phy@fe8a0000` são **idênticos** (só os rótulos dos botões mudam). O
  `rk817-charger/online` é `0` no X35H contra `1` no X55.
- **X35H — rádio:** `mmc3: Failed to initialize a non-removable card` → **sem Wi-Fi e
  sem BT** (`wlan0` ausente, `hci0` sem endereço). Nós de mmc idênticos aos da X55.
- `usb-watch` passou a gravar o `dmesg` de USB **na foto inicial** (antes só "na
  mudança", e no X35H o estado nunca muda — foi por isso que o `ep0out` demorou).
- **Armadilha de CI que custou 3 tags:** a marca de skip vale no **corpo** da mensagem,
  então o commit que **documentava** a marca também foi ignorado (v0.7 e v0.8 não
  geraram build). Ao documentar, escreva "skip-ci" sem os colchetes.
- **Estado:** #119 (retroarch, 536 MB) e #120 (kodi) verdes. Falta: testar um jogo de
  verdade; no X35H, o teste do pendrive (decide se a porta é host) e o rádio (precisa
  medir `pwrseq`/regulador no aparelho).

### 2026-10-28 — Copilot (publicação das imagens no GitHub Releases)

- Fez: o job passou a **publicar a imagem no Releases**, porque o artifact do Actions
  expira e exige login. Gatilhos: tag `v*` (nova) e tag `retroarch-*` criam um release
  com o nome da tag; push na `main` (ou `workflow_dispatch` na `main`) atualiza a release
  rolante **`latest`**, marcada *pre-release*, com `gh release upload --clobber`.
  Adicionado `permissions: contents: write`; checksum **por arquivo**
  (`<imagem>.img.gz.sha256`) para as duas variantes conviverem no `latest`.
- **Formato:** continua sendo `.img.gz` de cartão SD; **não** foi gerado ISO9660 — o
  U-Boot é gravado nos setores 64/16384 do disco, então um ISO comum não inicializa o
  aparelho. As notas do release dizem isso e explicam como gravar.
- Validado **sem gastar build**: YAML parseado, os `run:` extraídos e verificados com
  `bash -n`, e os dois passos simulados com um `gh` falso (release inexistente →
  `create`; release existente → `edit`/`upload --clobber`), além do heredoc das notas.
- Pendente / próximo: confirmar no primeiro run verde que o release nasce no servidor
  (o teste local usou `gh` simulado) e que `latest` recebe o asset da variante do push.

### 2026-10-28 (2) — Copilot (no RetroArch, sair do app desliga o aparelho)

- **Pedido:** o menu do RetroArch só tem "Quit RetroArch", e fechar apenas o app deixava
  o aparelho ligado — sem interface (o `tty1` é do app e o `getty@tty1` é mascarado) e
  sem como desligar sem SSH/serial.
- **Não existe item "Desligar"** no RetroArch 1.14 (o menu é `Restart RetroArch` +
  `Quit RetroArch`); criar um exigiria patch no fonte do RetroArch. Solução: o
  **launcher** trata saída normal como desligamento.
- Fez: `start-retroarch.sh` lê `retroarch_exit` de `/boot/walkmam.conf` (padrão
  `poweroff`; `stay` = só sai) e, ao terminar com status 0 e sessão ≥ 10 s, mostra
  "Desligando..." no tty1, faz `sync` e `systemctl poweroff --no-block`. Crash ou sessão
  curta **não** desligam (continua sendo falha, com retry e motivo na tela). Criado
  `walkmam_read_conf_key` em `display.sh` (`walkmam_read_transform` passou a usá-lo) e a
  chave foi documentada no `walkmam.conf` gerado pelo CI e pelo `build/create-image.sh`.
  `quit_press_twice` continua ligado, para não desligar com um toque acidental.
- Testado **sem hardware**: launcher rodado com `retroarch`/`systemctl` falsos — 6 casos
  (conf ausente, `stay`, `poweroff`, valor inválido, crash, sessão curta) e o parsing
  real do `display.sh` com `walkmam.conf` em CRLF (FAT). Falta confirmar no aparelho.
- **Armadilha encontrada no teste:** `EXIT_ACTION=$(walkmam_read_conf_key ...)` com
  `set -eu` mata o launcher se o helper retornar != 0 — o padrão do projeto não tinha
  esse cuidado; agora a atribuição tem `|| EXIT_ACTION=`.
- **CONFIRMADO NO APARELHO (2026-10-28, X35H):** com o launcher novo instalado na
  imagem em uso, "Quit RetroArch" no menu mostrou "Desligando..." e **o aparelho
  desligou de verdade** — o gadget USB sumiu do PC (o que descarta queda de rede) e a
  interface de rede desapareceu. Os outros casos (`stay`, conf ausente, valor inválido,
  crash, sessão curta) foram testados **no próprio aparelho** com `retroarch`/`systemctl`
  falsos: só a saída normal de sessão longa desliga.
- Pendente / próximo: nada neste item. A tag `retroarch-v0.10` gera a imagem com isso.

### 2026-10-28 (3) — Copilot (1 eMMC + 2 slots SD, medido no X35H)

- **Pedido:** suportar 1 eMMC e 2 SD (o sistema pode estar no SD1 ou no eMMC; se estiver
  no eMMC, sobram os 2 slots). Aparelho conectado (X35H) por rede para testar.
- **Medido no aparelho** (não inferido): `mmc0`/`mmc@fe310000` (sdhci-dwcmshc) = **eMMC
  `mmcblk0`, 3776 MiB**, com ROCKNIX de fábrica em GPT; `mmc1`/`mmc@fe2b0000` = **SD1
  `mmcblk1`** (o sistema); `mmc2`/`mmc@fe000000` = **SD2 `mmcblk2`** (128 GB, vfat);
  `mmc3`/`mmc@fe2c0000` = SDIO do rádio (não é slot). **Nenhum slot tem `cd-gpios`**
  (todos `removable=0`).
- **Achados que quebrariam a imagem nova:** (1) `root=/dev/mmcblk1p2` estava fixo;
  (2) a imagem tinha **4096 MiB e não cabe no eMMC de 3776** (dd morreria no meio);
  (3) `BOOT_TARGETS` do U-Boot mainline é `mmc1 mmc0` — **o SD2 nunca era tentado**.
- Fez: `root=PARTUUID=<do build>` no CI e no `create-image.sh`; `BOOT_TARGETS "mmc1 mmc0
  mmc2 ..."` (define no header do board, antes do `#include` do `rk3568_common.h`, que
  respeita o `#ifndef`); imagem padrão **3584 MiB** (`WALKMAN_IMAGE_SIZE_MB`); novo
  `common/overlay/usr/local/bin/walkmam-extra-storage.sh` + unit (monta os slots sem o
  sistema em `/storage/sdcard<N>` e liga em `/boot/roms/sdcard<N>`) e
  `tools/install-image-to-emmc.sh` (confere tamanho, zera GPT, grava).
- **Hotplug sem `broken-cd`:** a X35H roda o **DTB oficial da ROCKNIX**, então mexer no
  nosso DTS não chegaria nela. Solução testada no aparelho: religar o slot
  (`unbind`/`bind` no `dwmmc_rockchip`) re-detecta cartão e partições. O serviço faz
  isso com cooldown e **nunca** toca o host que tem o root.
- **Testado no aparelho:** `root=PARTUUID` com **reboot real** (`VFS: Mounted root ... on
  device 179:130`, sem serviço em `failed`); serviço de armazenamento montou o SD2,
  criou `/boot/roms/sdcard2`, desmontou ao sumir o dispositivo e **remontou após o
  rescan**. Bug meu encontrado e corrigido no teste: `_rescan` abortava quando o
  `unbind` falhava (host já desligado) e nunca religava.
- **Não testado (honesto):** boot a partir do **eMMC** e do **SD2** — exigiria
  sobrescrever o ROCKNIX de fábrica, e o usuário pediu para não gravar agora. O que está
  provado é o `root=PARTUUID` (independente da mídia) e a detecção/montagem dos slots.
- **Correção de doc:** neste X35H o **USB gadget funciona** (`usb0` em 10.55.0.1, e foi
  assim que tudo foi medido) — o `failed to enable ep0out` não é de todo X35H.
- Próximo: gerar imagem e testar (a) boot com sistema no eMMC via
  `/usr/local/bin/install-image-to-emmc.sh`, (b) boot com o sistema no SD2, (c) inserir
  o cartão de dados com o aparelho ligado e ver `/boot/roms/sdcard2` aparecer.

#### Correção: run #124 falhou em "Create SD Card Image" (`/dev/loop0p1 does not exist`)

- **Sintoma:** `mount: /mnt/boot: special device /dev/loop0p1 does not exist`, depois de
  `mkfs.vfat`/`mkfs.ext4` darem certo e do `dd` do U-Boot.
- **Causa:** rodar `blkid` na **partição do loop** logo após o `mkfs` (`blkid -s PARTUUID
  -o value ${ROOT_DEV}`) fazia o kernel/udev reler a tabela e **derrubar os nós**
  `/dev/loop0pN` antes do mount.
- **Correção:** o PARTUUID de uma tabela DOS/MBR é `<assinatura do disco>-<NN>`, e a
  assinatura sai do **arquivo** com `sfdisk --disk-id` (não abre o loop). Conferido:
  `sfdisk --disk-id` → `0xd2ee80d2` e `blkid -p -s PTUUID` → `d2ee80d2`. Além disso, o
  CI agora espera/recria os nós (`partprobe` + `mknod` pelo major:minor do sysfs) antes
  de montar, e **confere** o PARTUUID contra a partição montada.
- **Lição:** em script de criação de imagem, **não** toque no loop device com `blkid`
  entre o `mkfs` e o `mount`; leia a assinatura do arquivo.

### 2026-10-30 — DeepSeek (análise da pasta + placa nova R35S/RK3326 + painéis)

- **Pedido:** analisar a pasta, entender/atualizar o status e **preparar o device
  `game-console-r35s`**, usando o repositório da KNULLI para achar o driver de display
  (a premissa era "eles escaneiam e acham o modelo de tela instalado").
- **Análise da pasta:** o estado do projeto é o de `docs/status.md` (#119/#120 verdes,
  X55 e X35H validados; X35H sem rádio e com dúvida de USB). **O workflow não foi
  tocado** de propósito — push na `main` gera build. Não há `git` nesta máquina, então
  nada foi commitado: as mudanças estão no working tree.
- **O que já estava na pasta (e estava errado):**
  `devices/game-console-r35s/` tinha `device.config` (nome inválido) com o conteúdo
  `1`, dois DTS **decompilados** de kernel de fabricante e dois READMEs vazios. Os DTS
  entregam a informação decisiva: a placa é **RK3326** (`rockchip,rk3326-odroidgo3-linux`,
  Cortex-A35, nós PX30), **não RK3566**.
- **Descoberta que muda o plano:** o pipeline atual é RK3566 (U-Boot
  `powkiddy-x55-rk3566`, `rk3568_bl31`, `rk3566_ddr`, DTS `rk3566-*`). Para o RK3326 é
  outro alvo: U-Boot **`odroid-go2`** (único rk3326 do mainline), `BL31=px30_bl31_v1.37.elf`
  e `ROCKCHIP_TPL=px30_ddr_333MHz_v2.12.bin` (o rkbin **não tem** binário chamado
  `rk3326`), e DTS da família `rk3326-*`. Nenhum DTS mainline é o R35S. Por isso o
  device ficou **preparado e recusado de propósito** (`image_dtb=` vazio), como a X35H.
- **Sobre a premissa do "escaneia e acha o painel":** **não existe** — nem na KNULLI
  (que usa kernel 4.4 de fabricante com o DTB **fixo** `rk3326-rg351mp-linux.dtb`), nem
  na ROCKNIX/REG Linux, cuja wiki manda *"descubra qual você tem, ou vá trocando até
  funcionar"* e distribui **overlays** (`overlays/mipi-panel.dtbo`, variantes
  `r36s-panel1..4`, `r36s_clone`, `rg351mp-v2`…). Painel DSI não tem EDID. O que foi
  feito é o mecanismo equivalente e verificável: **overlay `rocknix,generic-dsi` por
  variante de painel**.
- **Ferramenta nova e validada:** `tools/panel_overlay_from_dts.py` converte o bloco
  `panel@0` de um DTS de fabricante no overlay `rocknix,generic-dsi`. O formato foi
  **conferido contra o `.dtbo` oficial** `mipi-panel.dtbo.r36s-panel4` (release
  `panel_overlays`): `--check-arquivo` dá **160 strings idênticas**. Três armadilhas que
  só apareceram por causa dessa comparação:
  1. `flags` do fabricante `0xa03` precisa do bit `0x400` → **`0xe03`**;
  2. `horizontal`/`vertical` são **(display, front-porch, sync-len, back-porch)** — a
     doc do projeto estava ambígua (dizia hsync_start/htotal); corrigido em
     `docs/hardware-e-drivers.md` §4.1 com a conta que prova (`30 MHz/(990x518)=58,5 fps`);
  3. nós "50Hz"/"75Hz" do fabricante vêm com porches do 60 Hz (77,4 fps) e o overlay
     oficial recalcula o `vfront-porch` — regra implementada e marcada como **hipótese**
     (um ponto de dados), visível no cabeçalho do script.
- **Arquivos criados/alterados:** `devices/game-console-r35s/{device.conf,
  kernel.fragment,dts/Readme.md,uboot/README.md,dts/panel-overlay/*.dts}`,
  `docs/r35s-rk3326.md` (novo), `docs/status.md` (novo ponto de retomada 2026-10-30),
  `docs/hardware-e-drivers.md` §4.1, `AGENTS.md` (mapa + esta entrada), `README.md`
  (tabela de dispositivos + doc), `tools/panel_overlay_from_dts.py`. Removido o
  `device.config` placeholder.
- **Pendente / próximo (nesta ordem):**
  1. **Medir no aparelho com a imagem oficial** (não escrever DTS nosso antes):
     `cat /proc/device-tree/model`, qual DTB a imagem oficial usa, `evtest`/ADC,
     `lsblk` (TF1/TF2), `ls /sys/class/udc`, `rfkill list`. Isso decide o DTB base
     (candidato: `rk3326-anbernic-rg351m`) e evita chutar `gpio-keys`.
  2. Descobrir **qual variante de painel** esta unidade tem (trocar overlay até ligar).
  3. Só então criar o **job RK3326** no CI (checklist em `docs/r35s-rk3326.md` §5) —
     risco real: **blob de DDR** do px30 pode não servir no lote desta placa.
- **Não testado:** nada desta placa em hardware; `dtc`/`fdtoverlay` não rodaram aqui
  (não há `dtc` nesta máquina) — o overlay foi validado como **texto** contra o binário
  oficial, não compilado.

### 2026-10-30 (2) — DeepSeek (R36S: DTB, extlinux e overlays do aparelho conferidos)

- **Push do commit anterior:** feito com OAuth device flow (código de 8 dígitos,
  sem PAT). `1d0044d..9f87240` na `main`; run **#135** disparado. Autor do commit
  ficou `DeepSeek (agente) <253578272+xtrempkch-droid@users.noreply.github.com>`.
- **O usuário forneceu a partição de boot real** da imagem que funciona (ROCKNIX):
  5 `extlinux.conf`, 17 `.dtb` e 5 `mipi-panel*.dtbo`. Achados (todos medidos nos
  arquivos, não inferidos — tabelas em `docs/r35s-rk3326.md` §9):
  1. O `extlinux.conf` **ativo** é `FDT /rk3326-gameconsole-r36s.dtb` +
     `FDTOVERLAYS /overlays/mipi-panel4.dtbo` → **a unidade é panel4**.
  2. O `mipi-panel4.dtbo` do cartão deu `--check-arquivo`: **160 strings idênticas**
     ao que `tools/panel_overlay_from_dts.py` gera do `mipis-panel4.dts`.
  3. O `rk3326-r35s-linux.dtb` (vendor "fix by lxc") tem a **mesma** sequência
     embutida (773 bytes / 155 passos) com `elida,kd35t133` — é a origem do nosso
     `mipis-panel4.dts`.
  4. **`FDTOVERLAYS` existe no U-Boot que o projeto fixa** (`include/pxe_utils.h`,
     commit `0a55b09`, campo `char *fdtoverlays`). Isso derruba o plano antigo de
     "um DTB por variante de painel": dá para publicar `dtbs/` + `overlays/` e
     apontar no `extlinux.conf`, igual à ROCKNIX.
  5. Hardware do R36S lido do DTB: painel `gameconsole,r36s-panel` (`sitronix,st7703`,
     reset gpio3 p.16, LDO_REG8); joypad **`rocknix-singleadc-joypad`** (17 botões +
     2 analógicos por **multiplexador de ADC**, `saradc` canal 1); 2 slots SD
     (`mmc@ff370000`/`mmc@ff380000`) e **eMMC desligado**; USB otg com
     `role-switch-default-mode = "host"`; áudio `Speaker -> SPKO` (**≠ X55**, que usa
     HP); backlight PWM1 25 µs; bateria 3380 mAh; **sem Wi-Fi/BT**.
  6. O "escaneia e acha" é `uboot.hwid_adc` (ADC no U-Boot escolhe o **modelo** de
     placa), não o painel. `rocknix,device_switch` traz `this = "r36s"`.
- **Arquivos alterados:** `docs/r35s-rk3326.md` (§9 novo, fontes renumeradas),
  `devices/game-console-r35s/{device.conf,dts/Readme.md}` (painel4 + DTB base
  confirmados; `image_dtb` continua vazio de propósito).
- **Pendente / próximo:** falta **só o loader** (U-Boot + blob de DDR) para o nosso
  kernel — o caminho é o nome/versão da imagem ROCKNIX que funciona no aparelho, para
  eu baixar e extrair `idbloader.img`/`u-boot.itb` + `boot.scr` (ver §9.7). Depois:
  job RK3326 no CI (checklist §5) e o `rocknix-singleadc-joypad.c` + entrada do painel
  no `panel-sitronix-st7703.c` (ambos vêm da ROCKNIX, o CI já clona).

### 2026-10-30 (3) — DeepSeek (linha RK3326 separada no CI + loader mainline)

- **Pedido:** separar a linha RK3326 da RK3566 no Actions, deixar o usuário
  escolher o que construir no *Run workflow* e fazer o release carregar todas as
  versões.
- **Loader oficial analisado:** extraí da imagem ROCKNIX (2,05 GiB, sha256
  `ae5e4fcf…`) o layout (p1 FAT32 2 GiB em 16 MiB + p2 ext4 32 MiB), o
  `boot.scr` (decifrado: lê `hwid_adc` e escolhe DTB por faixa de ADC; **não há
  faixa para o R36S**, por isso o `extlinux.conf` fixa `FDT`), o `KERNEL`
  (arm64 Image, Linux **7.1.2**) e os 16 MiB de loader (**U-Boot 2017.09** de
  fabricante). **O idbloader oficial não é rkbin** (começa com código, não com
  `RK33`) — não é portável, mas também não é necessário.
- **Loader limpo é viável:** `odroid-go2_defconfig` existe no U-Boot fixado
  (`0a55b09`), usa `uart2` em `0xFF160000` (mesmo console do R36S a 1500000),
  tem `TPL_RAM`/`ROCKCHIP_SDRAM_COMMON` (DDR pelo blob `px30_ddr` do rkbin) e
  `OF_LIBFDT_OVERLAY`. Faltava só `DISTRO_DEFAULTS`/`CMD_SYSBOOT`/`FS_FAT`.
- **Feito:** `.github/workflows/build-rk3326.yml` (novo, **só workflow_dispatch**;
  a linha RK3566 em `build.yml` ficou intacta) com entradas `target`
  (`imagem`/`loader`), `variant` (`kodi`/`retroarch`) e `baudrate`; o job monta
  U-Boot mainline → kernel v6.12 + `panel-generic-dsi.c` + `rocknix-singleadc-joypad.c`
  (commits fixos da ROCKNIX) → DTB oficial + overlay compilado → rootfs Debian →
  `build/build.sh`, e publica `rk3326-r36s-<variant>-sdcard.img.gz` (+`.sha256`)
  no **mesmo** release da linha RK3566 (assets acumulam, `--clobber` por arquivo).
  Removido `build-rk3326-loader.yml` (virou o job acima).
- **`build/create-image.sh`** ganhou três knobs por env, com os valores históricos
  como padrão: `WALKMAN_EARLYCON`, `WALKMAN_DTBS_GLOB`,
  `WALKMAN_PANEL_OVERLAY_NAME/_FILE` (este último escreve `FDTOVERLAYS` no
  `extlinux.conf`).
- **`devices/game-console-r35s`**: `image_dtb=rk3326-gameconsole-r36s.dtb`
  (binário oficial copiado para `dts/`, sha256 `b84245e6…`),
  `uboot_defconfig=odroid-go2`, `status=ci-rk3326-aguardando-teste-em-hardware`.
- **Armadilha de ferramenta (custou 2 idas e voltas):** o shell desta máquina é
  **Windows PowerShell 5.1**, onde `Set-Content -Encoding UTF8` grava **com BOM**
  e `Get-Content -Raw` lê arquivo sem BOM como ANSI → arquivos reescritos saíram
  com **dupla codificação** (`prÃ³prio`). Corrigido com um reparo em Python
  (`txt.encode("cp1252").decode("utf-8")`) e checado por BOM/`Ã`. **Não reescrever
  arquivo com acento usando `Set-Content`; usar a ferramenta de escrita ou Python.**
- **Validado localmente:** YAML parseia, 13 passos, `python` embutido compila,
  heredocs `PY`/`EOF` balanceados, knobs presentes no `create-image.sh`.
- **Pendente / próximo:** (1) rodar `target=loader` e testar num cartão que
  funcione com `instalar-loader.sh` (serial 1500000) — decide se o U-Boot
  mainline inicializa esta placa; (2) só depois `target=imagem`; (3) se o
  singleadc não compilar em 6.12, o log diz e o ajuste é localizado.

### 2026-10-30 (4) — DeepSeek (loader mainline REPROVADO no aparelho; pipeline usa o oficial)

- **Teste no hardware (feito pelo usuário):** imagem oficial do ROCKNIX com
  **só** os 16 MiB do loader trocados pelo mainline (`idbloader.img` 120 KiB com
  TPL `px30_ddr_333MHz_v2.12` + `u-boot.itb` 1,1 MiB com `px30_bl31_v1.37` e
  `odroid-go2`), imagem `sha256 488c4e55…`, gravação conferida por releitura.
  **Resultado: tela preta, sem rede/USB, nenhum sinal de vida** → o kernel nunca
  rodou, então a falha é antes dele.
- **Leitura honesta:** não prova que o U-Boot mainline rodou (sem serial não dá
  para separar DDR/TPL de falha no `sysboot`); prova que esse caminho não chega a
  executar o kernel. O `idbloader` do fabricante **não é rkbin** (`3b 8c dc fc…`,
  sem magic `RK33`): o DDR init é do miniloader deles, ajustado ao lote —
  hipótese principal de por que o blob genérico `px30_ddr` não serve.
- **Decisão:** o pipeline passou a usar o **loader oficial** por padrão
  (`loader=oficial`): baixa `ROCKNIX-RK3326.aarch64-20261001-a.img.gz` (sha256
  `d693d2bb…` pinado), confere e extrai os 16 MiB iniciais (`idbloader.img` =
  32 KiB..8 MiB, `u-boot.itb` = 8..16 MiB). `loader=mainline` continua como
  opção de experimento. O asset agora leva o loader no nome
  (`rk3326-r36s-<variant>-<loader>-sdcard.img.gz`).
- **Também nesta rodada:** correções no CI que custaram 2 runs — `ARCH_PX30` não
  existe no arm64 (é `ARCH_ROCKCHIP`) e `DRM_ROCKCHIP=y` exige `DRM=y` (senão o
  Kconfig rebaixa para `m`); e `rm` sem `sudo` do `qemu-aarch64-static` derrubava
  o rootfs na última linha. Com isso o kernel (21 min) e o rootfs passaram.
- **Pendente / próximo:** rodar `target=imagem` com `loader=oficial` e gravar;
  se a tela ligar, o próximo item é áudio/botões no hardware. Para retomar o
  loader limpo: console serial (ver §11.4 do doc) ou um U-Boot com `bootcmd` de
  rockusb para dar sinal visível no PC.

### 2026-10-30 (5) — DeepSeek (`dtb load fail` no boot: FDT tem que ficar na raiz)

- **Sintoma no aparelho (imagem nossa, loader oficial):** o U-Boot sobe e para em
  **`dtb load fail`**.
- **Causa:** o `create-image.sh` punha o DTB em `/dtbs/<nome>.dtb` (layout do
  RK3566, onde o U-Boot é mainline). O U-Boot 2017.09 de fabricante carrega o FDT
  da **raiz** — é o "parser pobre" que o README da ROCKNIX avisa que quebra até
  com espaço sobrando. O `LABEL` com parênteses/espaços era o segundo risco.
- **Correção (`build/create-image.sh`, retrocompatível):** knobs
  `WALKMAN_FDT_DIR` (padrão `dtbs`; vazio = raiz) e `WALKMAN_LABEL` (padrão
  `Walkmam (<device> - <variant>)`). O DTB passou a ser copiado **sempre também
  para a raiz**. No workflow do RK3326: `WALKMAN_FDT_DIR=` e
  `WALKMAN_LABEL=walkmam`.
- **DTBs novos que o usuário mandou:** `J:\boot\rk3326-rg351mp-linux.dtb` é o
  **mesmo** R35S/R36S "fix by lxc" (`elida,kd35t133`, 52x70 mm, 4 lanes,
  `flags 0x0a03`, init de 773 bytes/155 passos) que já tínhamos — só com o nome
  que a KNULLI usa; não acrescenta nada, mas confirma o painel.
  `J:\rk3326-odroidgo2-linux.dtb` é do **ODROID-GO2** (480x320, 1 lane,
  `odroidgo2-joypad`) e não serve.
- **Pendente / próximo:** rodar `target=imagem` (loader=oficial) e gravar; o
  `extlinux.conf` agora sai com `FDT /rk3326-gameconsole-r36s.dtb` e
  `LABEL walkmam`, igual ao que a ROCKNIX usa neste aparelho.

### 2026-10-30 (6) — DeepSeek (revisão com strings do loader de fábrica: `dtb load fail` não é fatal)

- **Pedido:** revisar o que fizemos e analisar como ROCKNIX/outros projetos usam
  o RK3326, em vez de seguir por tentativa.
- **Método:** ler o `uboot.img` de fábrica (8 MiB, já extraído da imagem ROCKNIX)
  com `strings` + env embutido — evidência direta, sem palpite.
- **Achado que derruba a hipótese anterior:** o `bootcmd` de fábrica é
  `boot_android ${devtype} ${devnum};bootrkp;run distro_bootcmd;`. O
  **`dtb load fail` é impresso pelo `bootrkp`** (passo Rockchip, antes do distro
  boot) quando ele não acha o dtb no SPI `resource` nem em
  `fatload mmc 1:1 ${fdt_addr_r} ${dtb_name}`. **`dtb_name` não existe no env
  padrão**, então esse passo falha *sempre* e escreve o `error.log` de 13 bytes —
  e, como os passos são separados por `;`, **não impede** o `distro_bootcmd`
  (que é quem lê o `extlinux.conf`). Ou seja: `dtb load fail` **não explica** o
  boot não ter subido; é ruído de um passo anterior.
- **Regra que fica:** o extlinux do RK3326 tem que imitar o arquivo da ROCKNIX —
  `FDTDIR /`, DTB na raiz, `LABEL` simples, sem espaço sobrando. Isso já está no
  `create-image.sh` (knobs `WALKMAN_FDT_DIR`/`WALKMAN_LABEL`).
- **Comparativo (docs §11.7):** ROCKNIX = vendor U-Boot + kernel **7.1.2** + 15
  DTBs + overlay; KNULLI = vendor U-Boot + kernel **4.4 BSP** + DTB fixo
  (`rk3326-rg351mp-linux.dtb`) + `boot.ini`. O nosso é o único **híbrido**
  (loader de fábrica + mainline 6.12 + DTB oficial + overlay) — ninguém testa
  essa combinação, então as arestas são nossas.
- **Pendente / próximo:** (1) bootar o cartão preparado (FDTDIR + DTB na raiz +
  cópias nos nomes que o `bootrkp`/distro boot podem tentar) e **observar se o
  USB/rede sobe** — se subir sem imagem, é painel/overlay, não kernel;
  (2) se não subir nada, o console serial (1500000 8N1) é o único caminho para
  ver o trecho pós-`bootrkp`, que é onde a falha real está.

### 2026-10-31 — DeepSeek (fonte única: kernel da própria ROCKNIX)

- **Entrada do usuário:** a imagem corrigida também deu **tela preta + nenhum
  `diagnostico.txt`** (não chegou ao userspace) e ele sugeriu **reduzir as fontes**
  para evitar conflito — sugestão aceita, e o diagnóstico abaixo confirma.
- **O problema real era mistura de árvores:** loader da ROCKNIX, **kernel mainline
  6.12**, **DTB da ROCKNIX (~7.1.2)** e overlay gerado do DTB de fabricante. O DSI
  usa bindings (`panel_description`, `video-mode`) que mudam entre 6.12 e 7.x — é
  justamente onde kernel e DTB precisam casar. E o aparelho liga com a ROCKNIX, que
  tem tudo da **mesma** release.
- **Feito:** nova entrada `kernelSource` no `build-rk3326.yml`:
  `rocknix` (padrão) extrai o **`/KERNEL` (7.1.2) da própria imagem ROCKNIX** —
  com um leitor de FAT em Python embutido no passo, que abre o MBR, lê o BPB, a FAT
  e a cadeia de clusters do arquivo — e o usa como `kernel/arch/arm64/boot/Image`;
  `proprio` mantém o build do 6.12. O download da imagem virou passo único (serve
  loader e kernel), o `modules_install` só roda em `proprio`, e o asset agora leva a
  fonte no nome (`...-kernel-<fonte>-sdcard.img.gz`).
- **Armadilha repetida (de propósito documentada):** `:` dentro de `name:` de um
  step quebrou o parse do YAML de novo — corrigido; e o Python embutido foi
  compilado localmente, como sempre.
- **Pendente / próximo:** rodar `target=imagem` + `loader=oficial` +
  `kernelSource=rocknix` e gravar. Se ligar, o casamento kernel/DTB era o problema
  e o passo seguinte é voltar ao 6.12 com DTB próprio (ou migrar para 7.x). Se não
  ligar, sobra `extlinux`/`booti`/loader e **serial passa a ser obrigatório**.

### 2026-10-31 (2) — DeepSeek (kernel da ROCKNIX tem initramfs embutido; caminho é kernel 7.x nosso)

- **Teste no hardware:** `kernelSource=rocknix` (loader + kernel 7.1.2 + DTB +
  overlay, todos da mesma release) **também não ligou** (tela preta, nada na FAT).
- **Causa provada pela config embutida do kernel** (extraída do próprio `Image` via
  `IKCFG_ST`): `CONFIG_INITRAMFS_SOURCE=".../initramfs.conf .../initramfs"`. O
  kernel da ROCKNIX **carrega initramfs próprio**, cujo `/init` procura o sistema
  deles pelos parâmetros `boot=`/`disk=`; com o nosso `root=PARTUUID` ele não monta
  o ext4 e para — por isso nada é escrito no cartão. Não era DTB, painel nem overlay.
- **Também verificado:** o nosso overlay é **funcionalmente idêntico** ao oficial
  `mipi-panel.dtbo.r36s-panel4` (mesma árvore, mesmo `target-path`, mesmas 160
  entradas); a diferença são **156 espaços à direita** — exatamente os 156 bytes de
  diferença (4865 vs 4709). O `--check-arquivo` anterior comparava texto normalizado
  e por isso dizia "idênticas".
- **Feito:** `kernelSource` voltou a `proprio` como padrão (com aviso na descrição)
  e entrou **`kernelVersion`** (`v7.1` padrão, `v6.12`, `v7.2`) no clone do
  `torvalds/linux` — kernel da **mesma geração do DTB oficial**, mas construído por
  nós (sem initramfs embutido, montando o ext4 Debian). Conferido que `v7.1` e
  `v7.2` existem como tag; `v7.1.2` não. Nome do asset agora leva a versão quando a
  fonte é `proprio` (`...-kernel-proprio-v7.1-sdcard.img.gz`).
- **Armadilha minha, corrigida na hora:** o `if` do sufixo da versão foi parar
  dentro do bloco `sudo env` (que é lista de atribuições) — quebraria o shell e o
  YAML. Movido para fora; validado de novo.
- **Pendente / próximo:** rodar `target=imagem` + `kernelSource=proprio` +
  `kernelVersion=v7.1` e gravar. Se ainda não ligar, **serial é obrigatório** —
  já eliminamos loader, DTB, overlay, cmdline e rootfs como suspeitos plausíveis.

### 2026-11-01 — Copilot (variante `media-player`: app de mídia único em C)

- **Pedido do usuário:** criar a variante `media-player` (a pasta já existia com 5
  protótipos HTML em `layout/`, que são só referência visual). Na primeira versão eu
  fiz Weston + `cog`/WebKit + `mpv` + backend HTTP em Python; o usuário **vetou**: "faça
  desse um app de reprodução único. nada de usar outra camada já existente". Tudo foi
  reescrito.
- **O que é hoje:** um binário em C (`variants/media-player/src/`, ~5 mil linhas) que é a
  camada inteira:
  | Arquivo | Responsabilidade |
  | --- | --- |
  | `drm.c` | KMS: dumb buffer, page flip com vblank, **rotação por software**, `KD_GRAPHICS` no tty0 para o fbcon não brigar |
  | `decoder.c` | libavformat/libavcodec/libswresample/libswscale, thread por arquivo, áudio como relógio, descarte de quadro atrasado |
  | `audio.c` | ALSA S16 estéreo; `audio_write()` bloqueia e por isso **dita o ritmo**; volume pelo mixer + software |
  | `gfx.c` / `font.c` | canvas XRGB8888 na CPU (cantos arredondados, gradiente) e texto com FreeType (cache de glifos) |
  | `library.c` | varredura recursiva em segundo plano + tags ID3v2/ID3v1/Vorbis (lidas do arquivo, sem abrir o FFmpeg por faixa) |
  | `input.c` | evdev do `rocknix-joypad` + teclado USB; analógico com auto-repeat |
  | `radio.c` | estado do kernel + `nmcli`/`bluetoothctl` (os serviços que a imagem já tem) |
  | `ui.c` | 4 abas: Música, **Vídeos (tela cheia)**, Rádio (com teclado na tela), Sistema |
  | `main.c` | laço principal, bateria/brilho/uptime, desligar/reiniciar |
- **Não usa:** Weston, X11, WebKit/cog, mpv, ffplay, SDL, Node, Python, servidor HTTP.
- **Build:** `build/media-player.sh`. Cross `aarch64-linux-gnu-gcc` com os cabeçalhos
  **arm64 do bookworm** (é o que garante compilar contra o FFmpeg 5.1 da imagem, e não
  contra o do runner) e conferência de que o binário é `ARM aarch64`.
- **CI:** a variante entrou no input do `workflow_dispatch`, no gatilho por tag
  `media-player-*`, no Preflight (fontes/overlay presentes) e no bloco de rootfs
  (instala bibliotecas do manifesto, confere `ldconfig` + fonte DejaVu, compila, instala
  binário/launcher/serviço e roda `walkmam-media --version` **dentro do rootfs com
  qemu**). O `walkmam.conf` ganhou `media_volume`, `media_audio_out`, `media_roots` e
  `media_exit`; a imagem cria `/boot/media/{music,videos}` com LEIA-ME.
- **Testado (sem hardware):** `tools/test-media-player.sh` → biblioteca (ID3v2, ID3v1,
  Vorbis, fallback do nome), as 4 abas desenhadas com conteúdo (contagem de cores no
  canvas), o `--selftest` (teclas sintéticas: OK/L-R/setas/volume/B, inclusive "soltar
  a tecla não repete a ação"), vídeo H.264 de verdade decodificado e escalado (barras
  SMPTE conferidas cor a cor), e o launcher (falha propaga status ≠ 0, sessão curta não
  é sucesso, motivo no log). Também conferi as fontes contra os **cabeçalhos do
  bookworm arm64** (`--check`), que é onde `SwsContext` deixou de existir como typedef.
- **Bugs que apareceram na revisão e foram corrigidos:** `nmcli`/`bluetoothctl` eram
  chamados no caminho do desenho (a cada 1 s), travando a UI — agora um thread atualiza
  o estado a cada 5 s e a UI só lê o cache; o relógio do áudio não zerava no seek (o
  vídeo passaria a ser apresentado no ritmo errado); o pedido de seek era consumido por
  um `else if` inútil no `main`; a índex do foco dos controles era a mesma da lista.
- **Armadilhas que custaram tempo aqui:**
  1. `SwsContext` (typedef) não existe no FFmpeg 5.1 — usar `struct SwsContext *`.
  2. `snd_mixer_next_elem` não existe; o certo é `snd_mixer_elem_next`.
  3. `av_find_best_stream` devolve **código de erro negativo**, não `-1`: comparar
     sempre com `>= 0`.
  4. `--play` saía na hora porque o laço via `tocando=false` antes de o thread abrir o
     arquivo — esperar `aberto` primeiro.
  5. ID3v1 é **exatamente 128 bytes** no fim do arquivo; um "TAG" de 93 bytes não é lido
     (meu fixture de teste estava errado, não o parser).
  6. `TRIP`/`&` em `snprintf` com tamanho estimado gera `-Wformat-truncation`; use
     `%.*s`.
  7. **Link arm64 com sysroot baixado à mão:** no Debian o `.so` é *symlink* e o alvo
     só existe no pacote de **runtime** — baixar só os `-dev` passa no `-fsyntax-only`
     e falha em `cannot find -lavformat`. Por isso o sysroot sai de
     `build/media-player-sysroot.sh`, que baixa dev + runtime e **confere cada `-l`**
     (symlink e alvo) antes de compilar. Esse check roda em ~1 s e é o que evita um
     build de 40 min morrer no link.
- **Pendente / próximo (nada disso rodou no aparelho):**
  1. **DRM/KMS**: confirmar que o page flip funciona e que o `fbcon` não desenha por
     cima (o app pede `KD_GRAPHICS`; se piscar, olhar `/boot/media-player.log`).
  2. **Desempenho**: 720p é decode em software (sem VPU no mainline). Medir 480p e 720p.
  3. **Áudio**: confirmar que o mux do codec continua em `HP` (AGENTS.md §4.2.1) e que o
     mixer `Playback Volume` existe com esse nome no rk817.
  4. **Gamepad**: o mapa vem do DTS (`docs/hardware-e-drivers.md` §3.2.1); a zona morta
     do analógico (|v−512| > 200) foi calculada, não medida.
  5. Gerar a imagem: `git tag media-player-v1 && git push origin media-player-v1`.
- **Extra (achado no teste final):** a capa só aparecia com música tocando; agora, sem
  reprodução, ela vem da faixa destacada na lista (era o que faltava para o painel não
  ficar com o disco genérico). Validado decodificando um PNG de verdade e contando os
  pixels da capa no canvas.
