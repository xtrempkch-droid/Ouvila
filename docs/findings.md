# Descobertas de hardware e build (X35S/X35H e X55)

Este documento junta o que foi aprendido testando imagens nos aparelhos. Cada item
diz se foi **confirmado** (log ou teste no aparelho) ou é **hipótese**. Não trate
hipótese como fato ao portar outra placa.

## Aparelhos usados nos testes

| Aparelho | Observação |
| --- | --- |
| Powkiddy X35H | Sem WiFi, sem Bluetooth, sem touch. Compartilha o DTB da X35S. |
| Powkiddy X55 | Usado para corrigir a rotação do Kodi e o gamepad. |

## Bootloader e imagem (confirmado)

- U-Boot mainline com `powkiddy-x55-rk3566_defconfig`. A ROCKNIX usa o mesmo defconfig
  para a família, então a X35S/X35H também boota com ele.
- Blobs do rkbin usados pela ROCKNIX: `rk3568_bl31_v1.43.elf` e
  `rk3566_ddr_1056MHz_v1.18.bin`.
- Gravação no cartão: `idbloader.img` no setor 64 e `u-boot.itb` no setor 16384.
- O boot lê `extlinux/extlinux.conf` na partição FAT (`BOOT`). Se o `FDT` aponta
  para um arquivo que não existe, o aparelho acende o LED e fica sem imagem.

## Device tree

- **X55** tem DTS no kernel mainline (`rk3566-powkiddy-x55.dts`, desde o Linux 6.8).
- **Cuidado com drift da ROCKNIX (confirmado em 2026-10-23):** o `master` deles passou
  para o **kernel 7.2**, **removeu** o patch `0012` (joypad da X55) e reescreveu o
  `0016` (X35S) para usar `&gpio_keys_control`, label que **não existe** no DTS da X55
  do mainline 6.12. Resultado: `make dtbs` falha (`#90`/`#91`). O CI agora fixa a
  ROCKNIX em `8c081eb` (o estado que gerou a imagem validada) e falha cedo se algum
  patch de DTS for rejeitado.
- **X35S** não existe no mainline v6.12 (confirmado: `make dtbs` não gera o `.dtb`).
  O DTS vem de um patch da ROCKNIX (`add device tree for powkiddy x35s`) que só
  adiciona o arquivo. É preciso registrá-lo no `Makefile` de
  `arch/arm64/boot/dts/rockchip/`, senão o `.dtb` não é compilado.
- **X35H** não tem DTB próprio. A documentação da ROCKNIX manda usar o da X35S.
- O DTS da X35S tem ~41 linhas: faz `#include "rk3566-powkiddy-x55.dts"` e sobrescreve
  `model`, bateria, joypad e painel (640x480, `rocknix,generic-dsi`). **Confirmado**
  compilando e decompilando o `.dtb` — X35S e X35H compartilham esse mesmo DTB.

## Painel (confirmado)

- O painel usa `compatible = "rocknix,generic-dsi"`. Esse driver não está no mainline.
  É o `panel-generic-dsi.c` do repositório `ROCKNIX/distribution`, injetado no
  kernel pelo `package.mk` deles.
- Com o driver habilitado (`CONFIG_DRM_PANEL_GENERIC_DSI`), o log mostra
  `panel-generic-dsi ... lanes 4` e o framebuffer é criado.
- O nó `hdmi@fe0a0000` precisa estar desabilitado. No `.dtb` oficial da ROCKNIX com
  HDMI e DSI ligados juntos, o log mostra `Failed to register component: -517` e
  `POST_BUF_EMPTY irq err at vp1` em loop, com tela preta. Desabilitando o HDMI (por
  decompilar, editar e recompilar o `.dtb`), a tela ligou.
- No X55 aparece o mesmo erro `-517`/`POST_BUF_EMPTY`; o workflow também desabilita
  o HDMI nele para testar. O Kodi informou ter escolhido `DSI-1`, então o log não
  prova que ele tenha escolhido HDMI.

## Botões e analógico

- Sem o driver certo, o kernel só enumera `rk805 pwrkey` e `gpio-keys-vol`.
  (confirmado em `diagnostico.txt`).
- O driver é o `rocknix-joypad`, de um repositório separado
  (`github.com/ROCKNIX/rocknix-joypad`). Ele usa `input-polldev.h`, que não existe
  mais no Linux 6.12. Um header de compatibilidade baseado em `input_setup_polling`
  resolve a compilação.
- No X55, depois de corrigir o DTS, o kernel registrou `retrogame_joypad` com 17
  botões e 4 eixos, e o Kodi reconheceu o joystick (confirmado).
- O nó `rocknix-joypad` precisa ser filho direto da raiz do device tree, porque o
  driver é um `platform_driver`. Aninhado sob o DSI, ele não é registrado.
- O mapa do Kodi (`retrogame_joypad_17b_4a.xml`) só vale para 17 botões e 4 eixos.
  **Hipótese a verificar:** a X35H pode ter outra contagem.
- O arquivo `rocknix-singleadc-joypad.c` do mesmo repositório também usa
  `input-polldev.h`. Não foi confirmado que a X35S precise dele.

## USB gadget (ancoragem SSH pelo cabo): rede sobe; SSH exigia ajuste

- **Confirmado (verificado do PC em 2026-10-09, e de novo após reboot):** a rede USB do
  **X55 sobe** — o PC enumera `Linux-USB Ethernet/RNDIS Gadget`, obtém `10.55.0.2/24`
  (IP estático; o gadget **não** faz DHCP) e o aparelho responde em **10.55.0.1**
  (ICMP ok, porta 22 aberta). A ancoragem funciona; o que faltava era o SSH:
  o Debian 12 usa `PermitRootLogin prohibit-password`, então `root:root` era recusado.
  Corrigido no #89 com `/etc/ssh/sshd_config.d/10-ouvila.conf` — **validado**: login
  `ssh root@10.55.0.1` com senha `root` funciona.
- **Anterior (rodada com o DTB da X35S ativo):** a ancoragem não funcionava; isso
  combinava com o `dr_mode`/papel do controlador. Continua havendo a entrada
  `extlinux/extlinux.conf.x55-peripheral` como caminho de teste.
- **Confirmado (teste no aparelho, rodada anterior):** na X35H o kernel registra
  `dwc3 fcc00000.usb: failed to enable ep0out` e o PC não vê nada (nas duas portas
  testadas, com o `.dtb` oficial e com o nosso).
- **Confirmado (fontes):** o controlador OTG é `usb_host0_xhci` (`usb@fcc00000`,
  DWC3). No SoC base (`rk356x.dtsi`) ele sai com `dr_mode = "otg"` e
  `status = "disabled"`; o DTS da X55 o habilita (`status = "okay"`) com
  `phys = <&usb2phy0_otg>` **e `extcon = <&usb2phy0>`**. O `extcon` é quem decide o
  papel host/device.
- **Correção da hipótese:** no **X55** o gadget **anexou** (apareceu UDC e a `usb0`
  subiu), então a detecção de papel do OTG funciona nele. A hipótese do "sem UDC com
  `otg`" fica restrita ao **X35H**, onde ainda se vê `failed to enable ep0out`.
- **Fonte confirmada:** o DTS da X35S da ROCKNIX é
  `#include "rk3566-powkiddy-x55.dts"` e só sobrescreve `model`, `battery`,
  `joypad` e `panel`. Não há nó de USB próprio — então X35S e X55 compartilham a
  mesma configuração de USB.
- **`getty@tty1` disputava o tty1 com o Kodi (corrigido em 2026-10-24):** o
  `kodi.service` declara `Conflicts=getty@tty1.service` e usa `TTYPath=/dev/tty1`, mas a
  imagem também habilitava autologin do getty no **mesmo** tty1. No boot os dois
  disputam a unidade e um deles leva SIGTERM — no log isso aparece como
  `Kodi (wayland) terminou com status 143` seguido de tentativa GBM
  (`ERROR: Unable to create GUI. Exiting`, status 255) e reinício do serviço. Agora o
  `getty@tty1` é **mascarado** (o tty1 é do Kodi) e o shell fica no console serial
  `ttyS2`, que tem autologin do `root`.
- **Como testar:** bootar com `dr_mode = "peripheral"` forçado no DTB ativo. A imagem
  oferece `extlinux.conf.x55-peripheral` (X55) e, para X35S/X35H, a entrada
  `extlinux.conf.x35s` já aponta para o DTB `peripheral` (e `x35s-otg` volta ao
  original). O `setup-usb-gadget.sh` também tenta, em best-effort, escrever
  `device` em `/sys/class/usb_role/*/role`, e grava tudo em `/boot/usb-gadget.log`.
- **Mecanismo do erro (fonte confirmada):** `failed to enable ep0out` vem de
  `dwc3_gadget.c`, quando `__dwc3_gadget_ep_enable()` falha para o endpoint 0 OUT. Esse
  comando de endpoint (DEPCMD) só completa com o controlador em modo *device*; em modo
  *host* ele estoura o timeout. Ou seja: o gadget tentou subir e o DWC3 estava como
  host. Daí a correção ser fixar o papel em `peripheral` no DTB, e não mexer em módulo.
- **Medido na X35H (2026-10-24):** vídeo (640x480 a 58,5 Hz, tela cheia), **áudio** e
  **botões/direcionais** funcionam. O USB é o único ponto em aberto.
- **`peripheral` NÃO resolveu (teste do usuário, 2026-10-24):** com
  `rk3566-powkiddy-x35s-rocknix-peripheral.dtb` (dr_mode confirmado `peripheral` no
  device tree em uso) o PC continua **não vendo nada**, nem "dispositivo desconhecido".
  Diagnóstico daquele boot:
  - `/sys/class/udc/fcc00000.usb` **existe** (o controlador registrou o gadget) mas
    `state = not attached` → o controlador não vê sessão/VBUS no fio;
  - `usb0` sobe com `10.55.0.1/24` e fica `NO-CARRIER`;
  - `failed to enable ep0out` continua no boot.
  Ou seja: o problema **não é o papel OTG** (forçá-lo a device não mudou nada).
- **O DTB não é o culpado:** desmontando o `.dtb` que a X35H realmente usou,
  `usb@fcc00000` tem `dr_mode = "peripheral"`, `phys`/`phy-names` corretos, `extcon`,
  `maximum-speed = "high-speed"`; `usb2phy@fe8a0000` e `otg-port` estão `okay`. É a
  mesma configuração estrutural do DTB da X55, que funciona.
- **Hipótese atual (não confirmada):** a porta USB-C do X35H **não está no controlador
  OTG** (`usb@fcc00000`). O RK3566 tem dois: `usb@fcc00000` (OTG, é o que ancora na X55)
  e `usb@fd000000` (`dr_mode = "host"`, usa `usb2phy0_host` + `combphy1` USB3), e **os
  dois sobem** nesta imagem. Se a porta física da X35H estiver no segundo, ela é
  **host-only**: ligar num PC nunca enumera nada (host contra host) e o gadget é
  impossível por software.
- **Teste que decide (2 minutos, precisa de pendrive):** com o aparelho ligado e o
  console serial aberto (`ttyS2`, 1500000n8), rodar `dmesg -w` e plugar um **pendrive**
  na porta USB-C (com adaptador OTG, se a porta for C). Depois `ls /sys/bus/usb/devices/`.
  - Apareceu dispositivo → a porta é **host**: dá para usar pendrive, teclado e
    **adaptador USB-Ethernet** (que resolve o SSH e o Wi-Fi do X35H); o gadget/SSH por
    cabo direto **não é possível** nessa placa.
  - Não apareceu nada e `cat /sys/class/udc/*/state` continuou `not attached` → a porta
    não tem dados (só carga) ou o cabo é só-carga. **Repetir com o MESMO cabo que
    funciona na X55** (controle) antes de concluir.
- **Ferramenta nova na imagem:** `ouvila-usb-watch.service` roda
  `usb-watch.sh` por 5 min no boot e grava `/boot/usb-watch.log` com um retrato do
  estado USB (UDC, `usb_role`, dispositivos em `/sys/bus/usb/devices`, `online` das
  fontes de energia) **no instante de cada mudança**, mais as últimas 40 linhas do
  `dmesg` naquele momento. Assim, plugar o cabo produz evidência em vez de opinião.
- Hipóteses ainda abertas: detecção de VBUS/ID, fornecimento do PHY, hub interno,
  cabo sem fios de dados, ou a porta ser host-only no hardware.

## Kodi e sessão gráfica

- O Debian bookworm traz **Kodi 20.1** (`2:20.1+dfsg-1`) e o build do Debian habilita
  `x11`, **`wayland`** e **`gbm`** (`debian/rules`: `BUILD_WAYLAND := yes`,
  `BUILD_GBM := yes`). Ou seja, `--windowing=wayland` **é** válido neste pacote.
- Com GBM direto, o Kodi usa o modo nativo do painel sem rotacionar. No X55 (painel
  720x1280) isso deixava a interface girada. A solução atual é Weston (backend
  DRM, shell kiosk) com rotação na saída `DSI-1` (o valor usado até agora, `270`, é
  inválido; ver abaixo).
- Em versões do Weston desta distro o backend se chama `drm-backend.so`, não `drm`.
- **CAUSA RAIZ do "Kodi não abre / tela em loop" (confirmada no aparelho,
  2026-10-23):** o **Weston 10.0.1 cria o socket como `wayland-1`** quando
  `WAYLAND_DISPLAY` está no ambiente, e não `wayland-0`. O launcher esperava por
  `wayland-0`, dava timeout em 30 s e matava um Weston que estava **funcionando**
  (o log mostrava DSI-1 720x1280, kiosk-shell e GL ES 3.1 prontos). O fallback para
  GBM então falhava (`Unable to create GUI`) porque o DRM ainda estava sendo liberado.
  Correção: `weston --socket=wayland-0` explícito + 3 s de espera antes do GBM.
  Validado com reboot: `kodi.service` ativo, Weston + Kodi em Wayland, `GUI format
  720x1280` e `xdg_output` lógico **1280x720** (rotação `rotate-90` aplicada e validada).
- **`transform=270` é inválido no Weston.** As man pages do Weston (`weston.ini(5)` e
  `weston-drm(7)`) listam só `normal`, `rotate-90`, `rotate-180`, `rotate-270` e as
  variantes `flipped-*`. O `weston.ini` do repo usava `transform=270`. O efeito exato
  de um valor inválido (ignorar a linha ou rejeitar a saída) **não foi verificado**;
  o `weston.log` mostra se ele registrou `Invalid transform`.
- **Hipótese consistente com os testes:** se o Weston ignorou o valor, a saída ficou
  `normal`. Isso explicaria a X35H "funcionar certinho" (ela quer `normal`) e o X55
  continuar sem a rotação desejada.
- **X35S e X35H:** a wiki da ROCKNIX dá a mesma tela (3,5", 640x480) e o mesmo DTB
  para as duas ("a X35H é a versão horizontal da X35S"). O DTS tem `rotation = <0>`.
  Não há evidência de rotação diferente entre elas; o esperado é `normal` nas duas.
  (Uma afirmação anterior deste projeto, de que a X35S precisaria de rotação própria
  por ser "vertical", não tem base e foi retirada.)
- **X55:** painel 720x1280 (retrato) com orientação DRM `left-up`. O sentido correto
  em nomenclatura do Weston **foi validado no aparelho: `rotate-90`**; por isso
  o valor é editável em `/boot/ouvila.conf` (ver `build.md`).

## Wi-Fi e Bluetooth (RTL8821CS) — confirmado em 2026-10-24

- **Hardware:** Wi-Fi é SDIO no `sdmmc1` (mmc3) e o Bluetooth é UART no `uart1`
  (`compatible = "realtek,rtl8821cs-bt"`). Os drivers do kernel já vinham certos:
  `RTW88_8821CS`, `BT_HCIUART_RTL`/`btrtl`, `RFKILL` (todos `=m` no config da ROCKNIX).
- **O que faltava (confirmado no aparelho):**
  1. **firmware** — `rtw88/rtw8821c_fw.bin` (Wi-Fi) e `rtl_bt/rtl8821cs_fw.bin` +
     `rtl8821cs_config.bin` (BT). Sem eles o kernel registra
     `Direct firmware load ... failed with error -2` e não cria `wlan0`/`hci0` utilizável.
  2. **`wpa_supplicant`** — sem ele o NetworkManager deixa `wlan0` em
     `unavailable` ("Scanning not allowed while unavailable").
  3. **BlueZ** (`bluetoothctl`, `bluetooth.service`) — não estava instalado, então não
     havia como parear/gerenciar nada.
- **Onde os firmwares estão:** o Debian não os traz no rootfs mínimo; o CI baixa do
  `linux-firmware` na tag **20260309** (a mesma que a ROCKNIX usa) com **sha256
  conferido** e instala em `/lib/firmware`. O upstream **não tem**
  `rtl8821cs_config.bin` (ver a seção de Bluetooth abaixo: o CI agora **gera** o
  arquivo).
- **Teste ao vivo (imagem #94):** com firmware + `wpa_supplicant` instalados,
  `wlan0` sobe e o scan lista redes (`nmcli dev wifi list`). **Porém o probe do boot
  falha**: o kernel carrega o firmware (`Firmware version 24.11.0`) e depois dá
  `sdio read32 failed ... -110` em série e `probe with driver rtw_8821cs failed with
  error -110`; um `modprobe -r/modprobe rtw88_8821cs` posterior faz o `wlan0` aparecer.
- **Causa e fix (fonte confirmada, ROCKNIX):** o patch
  `0004-arm64-dts-rockchip-fix-wifi-sdio-errors.patch` adiciona ao `&sdmmc1`:
  `no-mmc; no-sd; sd-uhs-sdr50;`. É exatamente o remédio para esses timeouts. O nosso
  workflow **não aplicava** esse patch porque só copiava arquivos com `x55`/`x35` no
  **nome**. Agora o build adiciona essas propriedades no DTS da X55.
- **Bluetooth (causa raiz e fix, confirmado no aparelho em 2026-10-24):** o BT é
  **UART no `uart1`** com `compatible = "realtek,rtl8821cs-bt", "realtek,rtl8723bs-bt"`.
  O kernel não tem driver para `rtl8821cs-bt`; quem casa é o fallback
  `realtek,rtl8723bs-bt` do `hci_h5` (**three-wire**), que usa o `btrtl` para baixar o
  firmware. O `h5_btrtl_setup()` chama `btrtl_get_uart_settings()`, que exige no
  `rtl8821cs_config.bin` uma entrada de offset **0x0c** (baudrate + flow control).
  O arquivo que o upstream publica com esse nome **não existe**; o `rtl8821c_config.bin`
  (10 bytes) renomeado não tem essa entrada → `RTL: no UART config entry found` →
  `-ENOENT` → o setup aborta e o `hci0` fica com `BD Address 00:00:00:00:00:00`
  ("No default controller available").
  - **Fix:** o CI gera o `rtl_bt/rtl8821cs_config.bin` (25 bytes) com assinatura
    `0x8723ab55`, uma entrada `0x0c` de 16 bytes, baudrate `0x04928002` (1500000) e
    `data[12] = 0x5f` (bit 2 = flow control, coerente com o `uart-has-rtscts` do
    `uart1`). Os demais bytes seguem o padrão da família (comparados com os configs
    reais do 8723BS e do 8822CS do `linux-firmware`).
  - **Resultado:** `RTL: fw version 0x75b8f098`, `hci0` com endereço real
    (`A8:4F:A4:C9:98:4C`), `UP RUNNING`, nome `rk3566-x55`, e o scan lista
    dispositivos. Confirmado também **depois de reboot** — sobe sozinho no boot.
  - O aviso `of_dma_request_slave_channel: dma-names ... missing` (patch `0021` da
    ROCKNIX) também já está aplicado como override no DTS.
- **Painel web validado no aparelho (#94):** subindo `index.html` + CGIs + serviço
  manualmente depois do boot, do PC sobre o cabo USB deu `HTTP 200` no índice,
  `wifi.sh?action=status` e `action=scan` retornaram JSON correto (redes listadas) e
  `bt.sh?action=status` idem. Ou seja, o painel em si funciona; o que faltava para o BT
  era o controlador inicializar (fix de DTS acima).
- **Rede de segurança:** `ouvila-radio-retry.service` roda após o boot e, se `wlan0`
  não existir, recarrega o módulo (até 3x); se `hci0` estiver sem endereço, reinicia o
  `hci_uart`/BlueZ. Log em `/boot/radio-retry.log`.
- **Gerenciamento:** o Kodi 20 no Linux **não tem** UI de rádio (o gerenciador de
  Wi-Fi do Kodi existe apenas nos builds Windows/Android; no Linux não há nada no
  menu). Por isso o projeto entrega duas interfaces:
  1. **Add-on `script.ouvila.radio`** ("Ouvila Radio") — diálogos nativos do Kodi em
     **Add-ons → Add-ons de programas**. Usa `nmcli` para Wi-Fi e `bluetoothctl` para
     BT. É um `xbmc.python` 3.0.0 (script add-on), instalado em
     `/usr/share/kodi/addons/script.ouvila.radio/`. O CI checa a sintaxe do
     `default.py` (`py_compile`) para o build falhar antes em vez de dar erro só ao
     abrir no aparelho.
  2. **Painel web** (`ouvila-webui.service`, busybox httpd na porta **8088**) para
     quando se está no PC ou no celular: `http://10.55.0.1:8088` pelo cabo ou no IP do
     Wi-Fi.
  Cuidados ao mexer no add-on: `bluetoothctl` imprime `[NEW] Device <MAC> <nome>` no
  scan e `Device <MAC> <nome>` em `devices`, então o parser não pode ser ancorado no
  início da linha; e as linhas `[CHG] ... RSSI:` não trazem nome (usar só `[NEW]`).
  **Teclado da senha (bug corrigido em 2026-10-24):** o `xbmcgui.Dialog().input()`
  recebe o tipo de teclado em `type` e a flag de ocultar em `option`. Medido no
  aparelho com `Input.SendText` + leitura do retorno: `type=0,option=0` -> `'abC123xy'`;
  `type=0,option=64` -> `'abC123xy'` (oculto); `type=5` -> `''`; **`type=64` -> `''` e o
  teclado nem abre**. Passar `ALPHANUM_HIDE_INPUT` (64) no `type` — o que o add-on fazia
  — é o que dava a impressão de "só aceita números". O correto é
  `type=INPUT_ALPHANUM, option=ALPHANUM_HIDE_INPUT`.
  **GUI validada no aparelho (2026-10-24)** usando o JSON-RPC do Kodi em TCP **9090**
  (o `--webserver` da 8080 está desligado; a 9090 responde sem habilitar nada):
  `Addons.GetAddonDetails` devolveu `{name: "Ouvila Radio", type: "xbmc.python.script",
  version: "1.0.0", enabled: True, broken: False}`; `Addons.ExecuteAddon` abriu o
  diálogo (`Select dialog`, controle focado `Wi-Fi: status e conexao`); navegar para
  `Wi-Fi: procurar redes` e selecionar rodou o scan e abriu a lista de verdade
  (`Kill.exe (100%) WPA2`). Zero tracebacks no `kodi.log`. Este é o caminho para
  testar o add-on por SSH sem tocar na TV.

## X35H: o USB falha no controlador, não no cabo (2026-10-25)

O log do boot da variante retroarch no X35H trouxe a primeira evidência de **nível de
controlador** — e ela encerra a linha de investigação "papel OTG / cabo / VBUS":

```
[    4.073446] dwc3 fcc00000.usb: failed to enable ep0out
```

Comparação dos dois aparelhos, mesmo kernel e mesmo DTB (o X35H usa o DTB oficial da
X35S do release ROCKNIX 20261001):

| | X55 (funciona) | X35H (não funciona) |
| --- | --- | --- |
| `failed to enable ep0out` | **ausente** | **presente** |
| `/sys/class/udc/fcc00000.usb/state` | `configured` | `not attached` |
| `rk817-charger/online` | `1` | `0` |
| `usb0` | sobe, 10.55.0.1 | `NO-CARRIER` |

`failed to enable ep0out` vem de `__dwc3_gadget_ep_enable()` (`drivers/usb/dwc3/
gadget.c`, chamado por `dwc3_gadget_start()`): o `ep0out` não aceita a configuração,
a inicialização do gadget aborta e o UDC nunca é levado a `configured`. Isso acontece
**antes** de qualquer negociação com o host, então nenhum ajuste de papel, cabo ou
`dr_mode` pode resolver.

Descartei a hipótese de DTB de forma definitiva: extraí `rk3566-powkiddy-x55.dtb` e
`rk3566-powkiddy-x35s.dtb` do release ROCKNIX e comparei os nós `usb@fcc00000`,
`usb@fd000000` e `usb2phy@fe8a0000` — são idênticos (mesmo `dr_mode = otg`, mesmo
`extcon`, mesmo `otg-port` com `status = okay`). O único diff relevante entre os dois
DTBs é o **rótulo** dos botões A/B e X/Y.

Leitura possível: a porta USB-C do X35H pode não estar fisicamente ligada a esse
controlador (o PMIC também não vê VBUS: `online = 0`). Teste que decide: plugar um
pendrive e ver se aparece algo em `/sys/bus/usb/devices/` — se aparecer, a porta é
**host** e um adaptador USB-Ethernet resolve rede/SSH; se nada aparecer em papel
nenhum, a porta é só de carga.

### X35H: o SDIO do Wi-Fi não inicializa (mmc3)

Mesmo boot, outro problema independente:

```
[    1.716222] mmc_host mmc3: card is non-removable.
[    2.194544] mmc3: Failed to initialize a non-removable card
```

`mmc3` (`mmc@fe2c0000`) é o SDIO do rádio (`cap-sdio-irq`, `mmc-pwrseq`,
`non-removable` no DTB) e o cartão não responde — por isso `wlan0` não existe e o
`radio-retry` tenta `rtw88_8821cs` três vezes sem sucesso (`hci0` também fica sem
endereço). Os nós de mmc são idênticos aos da X55, então é sequência de energia do
módulo no X35H, não configuração. **Consequência prática: o X35H está sem Wi-Fi e sem
Bluetooth nesta imagem**, e não há como resolver por ajuste de software sem medir o
`pwrseq`/regulador no aparelho.

## Variante retroarch: dois bugs de sessão gráfica (2026-10-24, no aparelho)

Os dois só aparecem quando o RetroArch roda de verdade; nenhum é visível no build.

### 1. Wayland derruba o cliente por causa do tamanho da superfície

Sintoma: serviço sobe, `retroarch` morre em ~7 s **com status 0**, tela preta, e o
launcher considera sucesso (nada é escrito no tty1).

```
=== /boot/retroarch-start.log ===
start-retroarch (transform=rotate-90)
Weston pronto; iniciando RetroArch (video_driver=wayland)
xdg_wm_base@11: error 4: xdg_surface geometry (720 x 1280) is larger than the
configured fullscreen state (1280 x 720)
RetroArch (wayland) terminou com status 0
```

Causa: o Weston gira a **saída** (`transform=rotate-90` → 1280x720), mas o RetroArch
dimensiona a **própria superfície** com `video_fullscreen_x/y`; com esses valores em
`"0"` (padrão) ele usa o modo **nativo** do painel (`720x1280`) e pede uma superfície
maior que o fullscreen configured → violação de protocolo → Weston mata o cliente.

Correção (confirmada no aparelho): calcular o tamanho da saída já girada a partir do
modo do DRM + `display_transform` e passar por `--appendconfig`
(`video_fullscreen_x/y`). Medido: com 1280x720 o processo fica vivo indefinidamente
(antes morria em 7,5 s), sem nenhuma linha de erro no log do Weston.

Efeito colateral encontrado: qualquer saída com **status 0 e sessão curtíssima** não é
"o usuário saiu do menu". O launcher agora só considera sucesso se durou ≥ 10 s; senão
tenta o driver `drm` e, se também falhar, escreve na tela e no log.

### 2. Nenhum botão responde: o RetroArch do Debian não tem perfil de joypad

Diagnóstico objetivo, sem depender do que aparece na tela (driver de vídeo nulo, só
para não tomar o display):

```
retroarch --appendconfig <(video_driver="null" audio_driver="null" input_driver="udev")
[INFO] [udev]: Pad #0 (/dev/input/event3) supports 0 force feedback effects.
[INFO] [Autoconf]: retrogame_joypad (18507/4353) not configured.
```

Causa: `dpkg -L retroarch | grep autoconfig` volta **vazio** — o pacote não traz
nenhum perfil, e o projeto `retroarch-joypad-autoconfig` também não tem perfil para
`retrogame_joypad`. Sem perfil, nenhuma ligação é criada.

Correção (confirmada no aparelho): perfil próprio em
`/root/.config/retroarch/autoconfig/udev/Rocknix_Gamepad.cfg`, com `input_device`,
`input_vendor_id = "18507"`, `input_product_id = "4353"` e os índices calculados a
partir do bitmap real (`/sys/class/input/event3/device/capabilities/key =
"f00000000 0 0 0 7fdb000000000000 0 0 0 0"` → 17 botões, índices 0–16). Depois do
perfil: `[Autoconf]: retrogame_joypad configured in port 1.` e, no teste do usuário,
todos os botões, D-pad, L/R/L2/R2, L3/R3, F e os dois analógicos corretos.

## Cosmético: leve esticamento horizontal (relato no aparelho, 2026-10-24)

- Sintoma: com a interface na orientação correta (`rotate-90`), a imagem fica
  **levemente esticada na horizontal**, "como em telas widescreen quando o sistema usa
  uma resolução um pouco menor".
- O `xdg_output` mostra **1280x720 lógicos** e o modo DRM é **720x1280@60**, ou seja, a
  proporção do buffer bate com a do painel — não é um scale do Weston por resolução
  errada.
- **Hipóteses (não verificadas):** (a) timings do modo do painel (`hx8394`) com
  porches/clock fora do ideal, gerando ajuste fino na controladora; (b) alguma opção
  de proporção/overscan do Kodi (pixel ratio / "Ajustar exibição"); (c) o painel físico
  não ter exatamente a proporção do modo declarado.
- **Medição (2026-10-24):** o driver mainline declara, para o painel da X55,
  `width_mm = 67`, `height_mm = 121` e modo `720x1280`. Proporções:
  **67/121 = 0,5537** (físico) contra **720/1280 = 0,5625** (modo) → pixel levemente
  não quadrado, **~1,6% mais largo que alto**. Isso explica o "esticado de leve para os
  lados" sem nenhum erro de escala do Weston/Kodi. É característica do painel, não bug
  de software; só um escalonamento não uniforme (~0,984 na horizontal) compensaria, e
  nenhum dos dois oferece isso de forma suportada.
- **Como confirmar em 1 minuto:** exibir no Kodi uma imagem com um quadrado perfeito
  em tela cheia; se ele parecer ~1,6% mais largo que alto, a medição fecha.
- **Onde olhar primeiro:** `/sys/class/drm/card0-DSI-1/modes` (só 720x1280), o log do
  Weston com `transform=rotate-90` e, no Kodi, `Settings → System → Display →
  Pixel ratio`/overscan. Não alterar timing do painel sem fonte confirmada.

## Itens que apareceram nos logs e ainda não foram tratados

- **Áudio (`asoc-simple-card: parse error`) — causa encontrada em 2026-10-23:** o
  `sound`/`hdmi-sound` ficavam em *deferred probe* porque o **I2S do Rockchip**
  (`CONFIG_SND_SOC_ROCKCHIP_I2S_TDM`, o CPU DAI `rk3568-i2s-tdm` do nó `i2s@fe410000`)
  **não estava no kernel**. O config da ROCKNIX traz `SND_SOC_ROCKCHIP_I2S=y`, mas o
  `make olddefconfig` do mainline descartava o filho (o pai `SND_SOC_ROCKCHIP` ficava
  unset). Correção no workflow: habilitar explicitamente `SND_SOC_ROCKCHIP`,
  `SND_SOC_ROCKCHIP_I2S` e `SND_SOC_ROCKCHIP_I2S_TDM`, falhar o build se sumirem, e
  **desabilitar `&hdmi_sound`** junto com o HDMI (senão ele nunca encontra o codec).
- **Áudio — estado no aparelho (build #97, 2026-10-24):** a placa `rk817_ext` aparece
  (`/proc/asound/cards`) e `aplay -l` lista `fe410000.i2s-rk817-hifi`; o
  `speaker-test` roda sem erro. O mux do codec (`Playback Mux`) fica em **`HP`** por
  padrão e **isso está certo no X55**: o DTS liga o amplificador de alto-falante
  (`spk_amp`) em **`HPOL`/`HPOR`**, não em `SPKO`, então o caminho do alto-falante é
  `DAC -> Playback Mux (HP) -> HPOL/HPOR -> Speaker Amp -> Internal Speakers`. O pin
  switch `Internal Speakers` está `on` e o `simple-audio-card,hp-det-gpio`
  (`gpio4 RK_PC6`) funciona: com nada plugado, `Headphones Jack` = `off`.
  **Não** mude o mux para `SPK`: o `SPK DAC` é o class-D interno do rk817 e não está
  roteado no DTS da X55 — em `SPK` o som simplesmente desaparece.
  Falta apenas a confirmação auditiva do usuário (tocar algo no Kodi).
- `va_openDriver() returns -1`: sem decodificação de vídeo por hardware via VA-API.
  O módulo `hantro_vpu` carrega e registra `/dev/video0` (dec) e `/dev/video1` (enc);
  falta o driver VA-API do Rockchip (`rockchip_drv_video.so`) no rootfs — investigar
  uso direto de V4L2/DRM PRIME pelo Kodi.

## Como diagnosticar sem serial, rede ou solda

O serviço `dump-diagnostico` grava `diagnostico.txt` na partição FAT (`/boot`)
depois do boot. Tire o cartão, abra no PC e leia. O arquivo inclui `dmesg`,
`/proc/bus/input/devices`, módulos, UDC, rede e logs do Kodi/Weston.
