# Hardware e drivers por dispositivo (referência de portabilidade)

Este documento existe para uma coisa: **se você for levar estes aparelhos para outro
sistema** (Arch ARM, Ubuntu, Batocera, outra árvore de kernel, um fork da ROCKNIX…),
você já tem aqui a arquitetura de hardware, a arquitetura de driver de cada bloco, os
números exatos (GPIO, trilhos, timings, firmware) e as correções que foram necessárias
— prontas para copiar.

> **Regra do projeto:** nada aqui é inferido. Cada linha está marcada como
> **CONFIRMADO** (medido no aparelho ou lido no código-fonte) ou **HIPÓTESE**
> (dedução ainda não testada). Se você mudar algo e o resultado divergir, o documento
> está errado — não o seu teste.

Base de referência: **Linux v6.12 mainline** + **U-Boot** + **DTS mainline da X55**,
com trechos da **ROCKNIX** (fixada por commit) que ainda não estão upstream.

---

## 1. Resumo: o que muda entre os três aparelhos

| Item | Powkiddy X55 | Powkiddy X35S | Powkiddy X35H |
| --- | --- | --- | --- |
| DTB no boot | `rk3566-powkiddy-x55.dtb` | `rk3566-powkiddy-x35s.dtb` | **usa o mesmo DTB da X35S** |
| Fonte do DTS | mainline v6.12 (`rk3566-powkiddy-x55.dts`) | ROCKNIX (patch), **inclui o DTS da X55** | idem X35S |
| Painel | Himax HX8394, `powkiddy,x55-panel`, 720x1280 | driver da ROCKNIX `rocknix,generic-dsi`, 640x480 | idem X35S |
| Rotação de interface | `rotate-90` | `normal` | `normal` |
| Eixos analógicos | normais | **invertidos** (`invert-absx` / `invert-absy`) | idem X35S |
| Botões A/B/X/Y | `BTN_EAST/SOUTH/WEST/NORTH` | **remapeados** no DTS | idem X35S |
| Bateria (design) | 4 000 000 µAh | 3 000 000 µAh | idem X35S |
| Wi-Fi/BT (RTL8821CS) | **sim**, confirmado funcionando | **não medido** (o DTS herda os nós da X55; se a placa tiver o chip, funciona) | **não tem** (registrado no projeto) |
| HDMI físico | não | não | não |
| Validado em hardware | **sim** (2026-10): vídeo, áudio, botões, rádio, USB | **não** | **sim, parcial** (2026-10-24): vídeo, áudio e botões OK; **USB não ancora** (provável hardware) |

Os três são RK3566 (4 núcleos Cortex-A55). Na **X55** medido: `MemTotal` ≈ 2 029 084 kB
(2 GB). A RAM da X35S/X35H não foi medida.

---

## 2. Cadeia de boot (igual nos três)

```text
eMMC/SD → BootROM → idbloader (rkbin: ddr + bl31) → U-Boot (extlinux)
        → /Image  +  /dtbs/<placa>.dtb  (FAT, partição 1, 128 MB)
        → rootfs ext4 na partição 2
```

* Console do kernel: **`ttyS2` @ 1500000n8**, `earlycon=uart8250,mmio32,0xfe660000`
  (CONFIRMADO no `extlinux.conf` e no `chosen/stdout-path = "serial2:1500000n8"`).
* O `extlinux.conf` ativo escolhe o DTB. **É o ponto nº 1 de erro de portabilidade:**
  a X55 com o DTB da X35S usa o driver de painel errado e fica com tela preta.
* A imagem é gravada e roda do **cartão SD** (`sdmmc0`, alias `mmc1` → `mmcblk1`).
  O DTS também habilita o eMMC (`sdhci`), mas na X55 testada **não há eMMC** — o kernel
  loga `mmc0: Failed to initialize a non-removable card` e segue. Inofensivo.
  (CONFIRMADO: `lsblk` só mostra `mmcblk1`.)
* `ouvila_x55_gamepad=rocknix-v1` na linha `APPEND` é **apenas um marcador** de
  identificação da imagem. Nenhum driver lê esse parâmetro — não espere mudança de
  comportamento dele.

---

## 3. Powkiddy X55 — arquitetura de hardware

### 3.1 Display (MIPI-DSI)

| Item | Valor | Fonte |
| --- | --- | --- |
| Controlador | Rockchip VOP2 + DWC MIPI-DSI | `CONFIG_DRM_ROCKCHIP`, `CONFIG_DRM_DW_MIPI_DSI` |
| Painel | Himax **HX8394** | `compatible = "powkiddy,x55-panel", "himax,hx8394"` |
| Driver | `drivers/gpu/drm/panel/panel-himax-hx8394.c` | descritor `powkiddy_x55_desc` |
| Modo | **720x1280@~60 Hz** | `powkiddy_x55_mode`: htotal 804, vtotal 1312, clock 63 290 kHz |
| Barramento | 4 lanes, RGB888, video burst + LPM, sem EOT | `hx8394_panel_desc` |
| Reset | `GPIO0_PD5` ativo em nível baixo | `reset-gpios` no nó do painel |
| Trilhos | `vcc` e `iovcc` = `vcc_lcd` | — |
| Backlight | **PWM** no `pwm4`, período 25 000 ns, alimentado por `vcc_sys` | `pwm-backlight` |
| Rotação no DTS | `rotation = <270>` | propriedade do nó `panel@0` |
| Nó DRM | `card0-DSI-1` (`connected`, modo `720x1280`) | medido no aparelho |

**Esticamento de ~1,6% (CONFIRMADO, é do painel):** o driver declara
`width_mm = 67` e `height_mm = 121`, ou seja razão 0,5537; o modo é 720x1280, razão
0,5625. A imagem sai ~1,6% mais larga que o "quadrado perfeito". É característica
física/EDID do painel, não escalonamento de software. Para eliminar, só com escala
não uniforme no compositor (não recomendado).

**Cuidado com as três camadas de rotação** — elas não são a mesma coisa:

1. `panel { rotation = <270> }` → diz ao DRM como o painel está montado (sentido
   físico). **Não é** o que gira o Kodi.
2. `weston.ini` → `transform=rotate-90` na saída `DSI-1` (arquivo gerado a cada boot a
   partir de `display_transform` em `/boot/ouvila.conf`).
3. Kodi → herda a saída já transformada do Weston; não tem transformação própria aqui.

Trocar só uma das camadas deixa a tela de ponta a cabeça ou com o toque/botões
deslocados.

### 3.2 Entrada (controles)

Há **dois** dispositivos de entrada, e a separação importa:

| Bloco | Driver | Nó DT | Observação |
| --- | --- | --- | --- |
| Botões + analógicos | **`rocknix-joypad`** (fora da árvore mainline) | `joypad: rocknix-joypad` | 17 botões GPIO + 4 eixos por **SARADC** |
| Voltar/volume | `gpio-keys` (mainline) | `gpio_keys_vol` | `KEY_VOLUMEDOWN` / `KEY_VOLUMEUP` |

Números do `rocknix-joypad` na X55 (CONFIRMADO no DTS e no aparelho:
`axes: 4, hats: 0, buttons: 17`, nome `retrogame_joypad`):

* `joypad-vendor = 0x484B`, `joypad-product = 0x1101`, `joypad-revision = 0x0100`
* Eixos: `io-channels = <&saradc 2>, <&saradc 3>, <&saradc 0>, <&saradc 1>` com os
  nomes `key-RY`, `key-RX`, `key-LY`, `key-LX`
* Escala ADC `2`, deadzone `216`, fuzz/flat `54`, tuning ±`180`, `poll-interval = 10`

Mapa dos botões (GPIO do RK3566, todos ativos em nível baixo):

| Botão | GPIO | Código | Botão | GPIO | Código |
| --- | --- | --- | --- | --- | --- |
| DPAD-UP | `gpio4 PA0` | `BTN_DPAD_UP` | START | `gpio4 PA2` | `BTN_START` |
| DPAD-DOWN | `gpio4 PA1` | `BTN_DPAD_DOWN` | BTN_F (MODE) | `gpio3 PB7` | `BTN_MODE` |
| DPAD-LEFT | `gpio3 PD6` | `BTN_DPAD_LEFT` | THUMBL | `gpio4 PA7` | `BTN_THUMBL` |
| DPAD-RIGHT | `gpio3 PD7` | `BTN_DPAD_RIGHT` | THUMBR | `gpio4 PB0` | `BTN_THUMBR` |
| BTN-A | `gpio3 PD3` | `BTN_EAST` | BTN_TR | `gpio3 PC6` | `BTN_TR` |
| BTN-B | `gpio3 PD2` | `BTN_SOUTH` | BTN_TR2 | `gpio3 PC7` | `BTN_TR2` |
| BTN-Y | `gpio3 PD4` | `BTN_WEST` | BTN_TL | `gpio3 PD0` | `BTN_TL` |
| BTN-X | `gpio3 PD5` | `BTN_NORTH` | BTN_TL2 | `gpio3 PD1` | `BTN_TL2` |
| SELECT | `gpio4 PA4` | `BTN_SELECT` | VOL− / VOL+ | `gpio4 PA3` / `PA5` | `KEY_VOLUME*` |

**Duas armadilhas do driver (as duas custaram build quebrado):**

1. O nó `rocknix-joypad` **precisa ser filho direto da raiz** do device tree. Se ficar
   aninhado (por exemplo dentro do `dsi`), o `platform_driver` não registra e não
   existe controle. O build valida isso em `build/configure-x55-dts.py`.
2. O driver da comunidade usa a API **`input-polldev`**, removida no Linux 6.12.
   O repositório traz `build/rocknix-input-polldev-compat.h`, instalado como
   `include/linux/input-polldev.h`, um adaptador fino para a API de polling atual.

#### 3.2.1 O que cada app precisa para enxergar esses 17 botões

O kernel entrega códigos evdev; cada aplicação precisa do **seu próprio mapa**. Os
dois mapas são arquivos diferentes, com convenções diferentes — e os dois já foram
fonte de "controle morto" neste projeto:

| App | Arquivo | Convenção |
| --- | --- | --- |
| Kodi | `.../addons/peripheral.joystick/resources/buttonmaps/xml/linux/retrogame_joypad_17b_4a.xml` | nomes de eixo/botão do SDL |
| RetroArch | `/root/.config/retroarch/autoconfig/udev/Rocknix_Gamepad.cfg` | **índice** na enumeração do driver udev |

**A armadilha do RetroArch:** os números do autoconfig **não são códigos evdev**. O
driver (`input/drivers_joypad/udev_joypad.c`) monta primeiro um bitmap de keycodes e
depois numera os botões presentes, varrendo nesta ordem: `KEY_UP..KEY_DOWN`,
`BTN_MISC..KEY_MAX`, `0..KEY_UP`, `KEY_DOWN+1..BTN_MISC`. Para os 17 botões da X55 o
resultado é:

| Índice | Código | Papel | Índice | Código | Papel |
| --- | --- | --- | --- | --- | --- |
| 0 | `BTN_A` (SOUTH) | B físico (baixo) | 9 | `BTN_START` | Start |
| 1 | `BTN_B` (EAST) | A físico (direita) | 10 | `BTN_MODE` | F |
| 2 | `BTN_X` (NORTH) | X físico (cima) | 11 | `BTN_THUMBL` | L3 |
| 3 | `BTN_Y` (WEST) | Y físico (esquerda) | 12 | `BTN_THUMBR` | R3 |
| 4 | `BTN_TL` | L | 13 | `BTN_DPAD_UP` | Cima |
| 5 | `BTN_TR` | R | 14 | `BTN_DPAD_DOWN` | Baixo |
| 6 | `BTN_TL2` | L2 | 15 | `BTN_DPAD_LEFT` | Esquerda |
| 7 | `BTN_TR2` | R2 | 16 | `BTN_DPAD_RIGHT` | Direita |
| 8 | `BTN_SELECT` | Select | | | |

Eixos: `ABS_X/Y/RX/RY` (0x1b) → índices `0,1,2,3` (esquerdo X/Y, direito X/Y). Se
algum botão for removido/acrescentado no DTS, **todos os índices acima dele mudam**.

Como recalcular sem adivinhar (não precisa de python no aparelho):

```sh
cat /sys/class/input/event*/device/capabilities/key   # bitmap, word alto primeiro
```

Decodifique os bits (`word_index*64 + bit`), filtre os códigos presentes e reordene
na sequência da varredura acima. O perfil do RetroArch é instalado pelo workflow a
partir de `variants/retroarch/overlay/root/.config/retroarch/autoconfig/udev/`.

### 3.3 Áudio

| Item | Valor |
| --- | --- |
| Codec | **RK817** (o PMIC também é codec), I²C `0x20` no `i2c0` |
| CPU DAI | `i2s1_8ch` no `i2s@fe410000`, `rockchip,trcm-sync-tx-only` |
| Placa | `simple-audio-card`, nome **`rk817_ext`** |
| MCLK | `mclk-fs = 256`, relógio `I2S1_MCLKOUT_TX` |
| Detecção de fone | `simple-audio-card,hp-det-gpio = <&gpio4 RK_PC6 GPIO_ACTIVE_HIGH>` |
| Amplificador | `spk_amp`: `simple-audio-amplifier`, `enable-gpios = <&gpio4 RK_PC2 ACTIVE_HIGH>` |

Roteamento (é o que explica o comportamento estranho do mixer):

```text
DAC → "Playback Mux" (HP) → HPOL/HPOR → Speaker Amp INL/INR → Internal Speakers
                          ↘ Headphones
```

* **O `Playback Mux` deve ficar em `HP`.** O amplificador do alto-falante é alimentado
  por `HPOL`/`HPOR`, não pelo `SPKO`. Colocar em `SPK` deixa o aparelho **mudo**
  (o `SPK DAC` é o class-D interno do RK817 e não está roteado nesta placa).
* `Internal Speakers` (`simple-audio-card,pin-switches`) permanece `on`.
* Com nada plugado, `Headphones Jack` = `off` — a detecção funciona.
* CONFIRMADO no aparelho: som no **jack 3,5 mm e no alto-falante interno**, com os
  dois caminhos corretos.

Módulos de áudio obrigatórios no kernel estão na seção 6.

### 3.4 Wi-Fi (RTL8821CS via SDIO)

| Item | Valor |
| --- | --- |
| Barramento | **SDIO no `sdmmc1`** (alias `mmc3`), 4 bits, `cap-sdio-irq` |
| Driver | `rtw88_8821cs` (`CONFIG_RTW88_8821CS=m`, sobre `rtw88_sdio` + `rtw88_core`) |
| Firmware | `/lib/firmware/rtw88/rtw8821c_fw.bin` |
| Energia | `vmmc-supply = &vcc_wifi` (`GPIO0_PA0`, ativo em nível baixo, 3,3 V) |
| Power sequence | `sdio_pwrseq`: reset em `GPIO0_PC0`, clock de 32 kHz do `rk817`, 200 ms |
| Confirmado | `mmc3: new ultra high speed SDR50 SDIO card` + `wlan0` no boot |

### 3.5 Bluetooth (RTL8821CS via UART)

| Item | Valor |
| --- | --- |
| Barramento | **`uart1`** (`serial@fe650000`), **4 fios**: `uart-has-rtscts` |
| Nó | `bluetooth { compatible = "realtek,rtl8821cs-bt", "realtek,rtl8723bs-bt"; }` |
| Wake/enable | `device-wake-gpios = GPIO0_PB6`, `enable-gpios = GPIO0_PC1`, `host-wake-gpios = GPIO0_PB5` |
| Driver efetivo | **`hci_h5`** (three-wire!) via o fallback `realtek,rtl8723bs-bt` + `btrtl` |
| Firmware | `rtl_bt/rtl8821cs_fw.bin` + `rtl_bt/rtl8821cs_config.bin` (**gerado**, ver F5) |
| Confirmado | MAC real (`A8:4F:A4:C9:98:4C`), `UP RUNNING`, scan listando dispositivos |

Não existe driver mainline para `realtek,rtl8821cs-bt`: quem casa é o segundo
`compatible`, `rtl8723bs-bt`, do `hci_h5` com `h5_data_rtl8723bs`. Ou seja **o chip é
tratado como H5 de três fios** mesmo o DTS declarando 4 fios com RTS/CTS — e isso
funciona, porque o `btrtl` liga o flow control por comando do próprio firmware.

### 3.6 USB (ancoragem / rede)

* Controlador: `dwc3` em **`fcc00000.usb`**, UDC exposto como `fcc00000.usb`.
* `dr_mode` **não** é definido no DTS da placa: vem do `rk356x.dtsi` como **`otg`**.
  Portanto a X55 **não precisa** de nenhuma variante "peripheral" — o papel é
  negociado por OTG. (CONFIRMADO: `state = configured`, `g_ether` carregado.)
* Gadget: `g_ether` → interface `usb0`, IP fixo **10.55.0.1/24**. **Não há DHCP**;
  do lado do PC configure estático `10.55.0.2/24`.
* Firmware/kernel: `CONFIG_USB_GADGET`, `CONFIG_USB_ETH` (módulo),
  `CONFIG_USB_DWC3{,_DUAL_ROLE}`, `CONFIG_USB_CONFIGFS_ECM`.

### 3.7 Energia e bateria

`battery: simple-battery` com 4 000 000 µAh de carga de projeto, carga terminando em
300 000 µA e corrente máxima de carga 2 000 000 µA. Quem carrega é o nó
`rk817_charger: charger` (drivers `rk817-charger` + `rk817_battery` do MFD `rk817`),
com `monitored-battery = <&battery>` e sense resistor de 10 000 µΩ.

No aparelho de teste a bateria estava desconectada e o kernel repetia
`Battery voltage ... below minimum voltage` — **isso é esperado sem bateria**, não é
defeito de driver. Não confunda com falha de leitura de ADC.

---

## 4. Powkiddy X35S — arquitetura de hardware

A X35S **não tem DTS próprio no mainline**. O que existe (vindo da ROCKNIX) é um `.dts`
que **inclui o DTS da X55** e sobrescreve o que muda:

```dts
#include "rk3566-powkiddy-x55.dts"

/ { model = "Powkiddy x35s"; compatible = "powkiddy,x35s", "rockchip,rk3566"; };

&battery { charge-full-design-microamp-hours = <3000000>; };

&joypad {
	invert-absx; invert-absy;
	sw5 { label = "GPIO BTN-B"; linux,code = <BTN_SOUTH>; };   /* A/B/X/Y remapeados */
	/* sw6 = BTN_EAST, sw7 = BTN_NORTH, sw8 = BTN_WEST */
};

&panel { compatible = "rocknix,generic-dsi"; panel_description = "..."; rotation = <0>; };

&hdmi { status = "disabled"; };
```

Consequências para portar:

1. **Painel diferente e sem driver mainline.** A X35S usa
   `rocknix,generic-dsi` — `drivers/gpu/drm/panel/panel-generic-dsi.c`, um driver da
   ROCKNIX que recebe a sequência de inicialização inteira como *string* em
   `panel_description` (timings + comandos DCS em hex). Esse arquivo **não existe no
   mainline**: para portar a X35S você precisa levar o `.c` junto e registrar
   `CONFIG_DRM_PANEL_GENERIC_DSI`. Sem ele o painel não inicializa nem com o DTB certo.
2. **Painel 640x480**, `format=rgb888`, `lanes=4`, `flags=0xe03`, clock ~30 MHz,
   vários modos candidatos (o `default=1` é o de ~58,5 fps).
3. **Eixos invertidos no DTS** (`invert-absx`, `invert-absy`). Se o analógico estiver
   com X/Y trocados no seu port, é aqui — não no Kodi.
4. **Botões remapeados** (`sw5..sw8`). A posição física A/B/X/Y difere da X55.
5. Bateria menor (3 000 mAh).
6. HDMI desligado (não existe porta física).

### 4.1 Como funciona o `panel_description` (o "device tree do painel")

O driver `panel-generic-dsi` não tem timings no código: ele **interpreta strings** do
device tree, uma por elemento da lista. Três prefixos (CONFIRMADO em
`drivers/gpu/drm/panel/panel-generic-dsi.c`):

| Prefixo | Função | Chaves aceitas |
| --- | --- | --- |
| `G` | globais do painel | `size=<largura_mm>,<altura_mm>`; `delays=<prepare>,<reset>,<init>,<enable>,<ready>` em ms; `format=rgb888` (também `rgb666`, `rgb666_packed`, `rgb565`); `lanes=<n>`; `flags=<hex>` |
| `M` | um modo de vídeo | `clock=<kHz>`, `horizontal=<hdisplay>,<hfront_porch>,<hsync_len>,<hback_porch>`, `vertical=<vdisplay>,<vfront_porch>,<vsync_len>,<vback_porch>`, `default=1` |
| `I` | um passo da sequência de init | `seq=<bytes em hex>`, `dcs=<cmd em hex>`, `data=<bytes em hex>`, `read=<n>`, `wait=<ms>` |

Exemplo da X35S, traduzido:

```text
"G size=70,52 delays=60,60,60,120,20 format=rgb888 lanes=4 flags=0xe03"
"M clock=30000 horizontal=640,150,50,150 vertical=480,20,6,12 default=1"   # 58,5 fps
"I seq=b9f11283"      # SETEXTC
"I seq=11 wait=250"   # SLPOUT + 250 ms
"I seq=29 wait=50"    # DISPON
```

> **Correção de 2026-10-30 (conferida contra overlay oficial):** os quatro números
> de `horizontal`/`vertical` são **display, front-porch, sync-len, back-porch** —
> não são `hsync_start`/`hsync_end`/`htotal` como este documento dizia antes.
> A prova está na conta do exemplo acima: `640+150+50+150 = 990` e
> `480+20+6+12 = 518`, e `30 MHz / (990 x 518) = 58,5 fps`, exatamente a taxa
> esperada. O overlay oficial do R36S (`mipi-panel.dtbo.r36s-panel4`) confirma a
> mesma leitura: o painel tem hfront=46/hsync=2/hback=44 e o overlay traz
> `horizontal=640,46,2,44`. O `tools/panel_overlay_from_dts.py` já gera nesse
> formato.
>
> **`flags`**: o bit `0x400` (`MIPI_DSI_MODE_VIDEO`) precisa estar ligado. O DTS
> de fabricante do R35S usa `0xa03` e o overlay oficial usa `0xe03`; com `0xa03` o
> `generic-dsi` não entra em modo vídeo.

Consequências práticas para portar:

* `flags=0xe03` é o `dsi->mode_flags` **cru**, somado dos bits do MIPI DSI
  (`MIPI_DSI_MODE_VIDEO | ... | MIPI_DSI_MODE_NO_EOT_PACKET`). Se o painel acender e não
  desenhar, o suspeito nº 1 é esse campo.
* Vários `M` = vários modos; o que tem `default=1` é o escolhido. Os comentários com
  `# fps=` e `(CAN FAIL)` vêm do processo de descoberta da ROCKNIX — os marcados como
  `CAN FAIL` podem não sincronizar.
* `delays` são **em ms e na ordem** prepare, reset, init, enable, ready. Encurtar o
  `init` costuma dar tela branca/preta intermitente.
* O `rotation` do nó `&panel` da X35S é `<0>`; a rotação da interface vem do Weston
  (ver F8).

**Não confirmado:** Wi-Fi e Bluetooth na **X35S** (o DTS herda os nós da X55; se a
placa tiver o RTL8821CS, deve funcionar com o mesmo firmware da X55), quantidade de
botões/eixos, e **nada foi testado em hardware**. Na **X35H** o projeto registra "sem
WiFi, sem Bluetooth, sem touch".

---

## 5. Powkiddy X35H — arquitetura de hardware

`devices/powkiddy-x35h/device.conf` está com `status=unsupported-no-independent-dtb` e
`image_dtb=` vazio: **não existe DTB independente**. Na prática a X35H usa o mesmo DTB
da X35S (`rk3566-powkiddy-x35s*`), e o modelo reportado no device tree é
`"Powkiddy x35s"` — não estranhe se `cat /proc/device-tree/model` disser x35s num X35H.

### 5.1 O que já foi medido no hardware (2026-10-24)

**CONFIRMADO no aparelho:**

* **Vídeo** — painel liga, tela cheia, modo preferido a **58,5 Hz**, sem necessidade de
  ajuste manual.
* **Áudio** — funciona.
* **Botões e direcionais** — funcionam (GPIO + eixos ADC), com o remapeamento A/B/X/Y
  e a inversão de eixos que o DTS da X35S aplica.
* **Rotação** — `normal` (painel 640x480).

* **Variante de jogos** (2026-10-25) — RetroArch em 640x480 via Wayland, sessão de
  81 s sem erro; os 17 botões e os 4 eixos do joypad são os mesmos da X55 (mesmo
  bitmap `key`/`abs`), então o perfil de autoconfig vale para os dois.

**Em aberto e medido (2026-10-25):**

* **USB — CORRIGIDO em 2026-10-28: funciona nesta unidade.** O item abaixo descrevia
  `failed to enable ep0out`; no aparelho testado em 2026-10-28 o **gadget subiu** e a
  rede USB (`usb0`, `10.55.0.1`) foi o caminho usado para SSH e para todo o
  levantamento de armazenamento. Ou seja: o defeito **não é de todo X35H** — pode ser
  unidade, cabo ou versão de U-Boot/DTB. Continua valendo medir caso a caso; o
  `usb-watch.log` mostra qual dos dois cenários aconteceu.
* **USB não ancora — e a falha é do controlador, não do cabo.** O `dmesg` do X35H tem
  `dwc3 fcc00000.usb: failed to enable ep0out`, que **não existe** na X55. É
  `__dwc3_gadget_ep_enable()` abortando dentro de `dwc3_gadget_start()`: o gadget não
  inicializa, o UDC nunca vai a `configured` (fica `not attached`) e `usb0` fica em
  `NO-CARRIER`. O `rk817-charger/online` é `0` no X35H e `1` na X55. Os nós USB/PHY
  são **idênticos** ao DTB oficial da X55 (comparei os DTBs do release ROCKNIX
  20261001), então não é device tree. Ver F12.
* **Sem Wi-Fi e sem Bluetooth:** `mmc3: Failed to initialize a non-removable card` — o
  SDIO do rádio (`mmc@fe2c0000`, `non-removable` + `mmc-pwrseq`) não responde, então
  `wlan0` não existe e `hci0` fica sem endereço. Os nós de mmc também são idênticos aos
  da X55: é sequência de energia do módulo neste aparelho.

### 5.2 Os três armazenamentos (eMMC + 2 slots SD) — medido em 2026-10-28

Mapa real, extraído do aparelho (`/sys/block/*/device` aponta para a controladora):

| Alias DT | Controladora | Driver | Mídia | Tamanho medido | Detecção de cartão |
| --- | --- | --- | --- | --- | --- |
| `mmc0` | `mmc@fe310000` | `sdhci-dwcmshc` | **eMMC** (`mmcblk0`) | **3776 MiB** | `non-removable` |
| `mmc1` | `mmc@fe2b0000` | `dwmmc_rockchip` | **SD1** (`mmcblk1`) | o cartão | **sem `cd-gpios`** |
| `mmc2` | `mmc@fe000000` | `dwmmc_rockchip` | **SD2** (`mmcblk2`) | o cartão | **sem `cd-gpios`** |
| `mmc3` | `mmc@fe2c0000` | `dwmmc_rockchip` | SDIO do rádio | — | `non-removable` |

Consequências que custam tempo se ignoradas:

1. **`root=/dev/mmcblk1p2` é frágil.** O sistema pode estar no eMMC (`mmcblk0`), no SD1
   (`mmcblk1`) ou no SD2 (`mmcblk2`) e o nome muda. A imagem usa **`root=PARTUUID=<do
   build>`** (o PARTUUID acompanha a imagem, então o mesmo `extlinux.conf` vale nas
   três mídias). Validado no aparelho: `VFS: Mounted root (ext4 filesystem) on device
   179:130` com `root=PARTUUID=40d50178-02`.
2. **O eMMC tem 3776 MiB.** A imagem já foi de 4096 MiB — **não cabia**, e um `dd` maior
   que o disco termina em `No space left on device` deixando instalação truncada. Agora
   o padrão é **3584 MiB** (`OUVILA_IMAGE_SIZE_MB` para mudar).
3. **O U-Boot mainline só varria `mmc1 mmc0`** (`BOOT_TARGETS` em
   `include/configs/rockchip-common.h`): um sistema no **SD2 nunca iniciava**. O build
   define `BOOT_TARGETS "mmc1 mmc0 mmc2 nvme scsi usb pxe dhcp spi"` no header do board
   (antes do `#include` do `rk3568_common.h`, que respeita o `#ifndef`).
4. **Nenhum dos dois slots tem `cd-gpios`** no DTS: o MMC core marca os dois como
   `removable=0` e **não percebe** cartão inserido depois do boot. `broken-cd` (polling)
   resolveria no nosso DTB, mas a X35H roda o **DTB oficial da ROCKNIX**, então o
   hotplug é feito no software: `ouvila-extra-storage.service` religa o slot
   (`unbind`/`bind` no `dwmmc_rockchip`) — verificado no aparelho, o cartão e as
   partições voltam a ser detectados.
5. **O eMMC de fábrica tem ROCKNIX em GPT**; a nossa imagem é MBR. Ao instalar no eMMC,
   zere as tabelas antes (a GPT de backup continua no fim do disco e o kernel reclama
   `GPT PMBR size mismatch`). É o que `tools/install-image-to-emmc.sh` faz.

### 5.3 Por que os 75 Hz "não aplicam" (não é limitação do hardware)

O `panel_description` da X35S lista **vários** modos `M`, e o driver
`panel-generic-dsi` marca como **preferido** só o que tem `default=1` — no caso o de
**58,5 fps** (`clock=30000 horizontal=640,150,50,150 vertical=480,20,6,12`). O Kodi e o
Weston usam justamente o modo preferido do conector, então escolher outra taxa no
sistema não muda nada: a lista vem do device tree, não de EDID.

Os 75,47 fps existem na lista, mas marcados `# (CAN FAIL)` — ou seja, o processo de
descoberta da ROCKNIX não conseguiu confirmar sincronia neles. Para realmente testar
75 Hz é preciso gerar um DTB em que esse `M` seja o `default=1` (e o de 58,5 perca o
marcador). Sem isso, o aparelho continua em 58,5 Hz por escolha do device tree.

Como conferir qual modo está ativo: `cat /sys/class/drm/card0-DSI-1/modes` (o primeiro
é o preferido) — o `diagnostico.txt` agora traz isso.

## 6. Configuração de kernel obrigatória

Sem estes símbolos o hardware **existe no DTS e não funciona**. Os valores abaixo são os
do build validado (CONFIRMADO no `.config` e nos logs do aparelho):

| Bloco | Símbolos |
| --- | --- |
| Display | `CONFIG_DRM_ROCKCHIP=y`, `CONFIG_DRM_DW_MIPI_DSI=y`, `CONFIG_DRM_PANEL_HIMAX_HX8394=y`, `CONFIG_DRM_PANEL_GENERIC_DSI=y` (X35S/X35H) |
| Backlight | `CONFIG_BACKLIGHT_PWM=y`, `CONFIG_PWM_ROCKCHIP=y` |
| Controles | `CONFIG_INPUT_JOYSTICK=y`, `CONFIG_JOYSTICK_ROCKNIX=y`, `CONFIG_ROCKCHIP_SARADC=y`, `CONFIG_INPUT_EVDEV=y` |
| Áudio | `CONFIG_SND_SOC_ROCKCHIP=y`, `CONFIG_SND_SOC_ROCKCHIP_I2S=y`, **`CONFIG_SND_SOC_ROCKCHIP_I2S_TDM=y`**, `CONFIG_SND_SOC_RK817=y`, `CONFIG_SND_SIMPLE_CARD=y` |
| Wi-Fi | `CONFIG_RTW88=m`, `CONFIG_RTW88_SDIO=m`, `CONFIG_RTW88_8821CS=m` |
| Bluetooth | `CONFIG_BT_HCIUART=m`, `CONFIG_BT_HCIUART_RTL=y`, **`CONFIG_BT_HCIUART_3WIRE=y`** |
| USB gadget | `CONFIG_USB_GADGET=y`, `CONFIG_USB_ETH=m`, `CONFIG_USB_LIBCOMPOSITE=y`, `CONFIG_USB_DWC3=y`, `CONFIG_USB_DWC3_DUAL_ROLE=y`, `CONFIG_USB_CONFIGFS_ECM=y` |
| Armazenamento | `CONFIG_MMC_SDHCI=y`, `CONFIG_MMC_SDHCI_OF_DWCMSHC=y` |
| GPU/vídeo | `CONFIG_DRM_PANFROST=m`, `CONFIG_VIDEO_HANTRO=m`, `CONFIG_VIDEO_ROCKCHIP_RGA=m` |

**A pegadinha do `olddefconfig` (custou uma imagem sem áudio):** o config da ROCKNIX
traz `CONFIG_SND_SOC_ROCKCHIP_I2S=y`, mas se o pai `CONFIG_SND_SOC_ROCKCHIP` estiver
unset, o `make olddefconfig` **descarta o filho silenciosamente**. Sem o I2S o
`simple-audio-card` fica em *deferred probe* para sempre (`asoc-simple-card: parse
error`) e não existe placa de som. Habilite pai **e** filhos, rode `olddefconfig` e
**asserte** que continuam ligados:

```sh
./scripts/config --enable SND_SOC_ROCKCHIP
./scripts/config --enable SND_SOC_ROCKCHIP_I2S
./scripts/config --enable SND_SOC_ROCKCHIP_I2S_TDM
make olddefconfig
for sym in CONFIG_SND_SOC_ROCKCHIP CONFIG_SND_SOC_ROCKCHIP_I2S \
           CONFIG_SND_SOC_ROCKCHIP_I2S_TDM CONFIG_SND_SOC_RK817 \
           CONFIG_SND_SIMPLE_CARD; do
  grep -qx "$sym=y" .config || { echo "!!! $sym caiu" >&2; exit 1; }
done
```

---

## 7. Firmware necessário

| Arquivo em `/lib/firmware` | Origem | Observação |
| --- | --- | --- |
| `rtw88/rtw8821c_fw.bin` | `linux-firmware` (tag fixada, sha256 conferido) | Wi-Fi |
| `rtl_bt/rtl8821cs_fw.bin` | `linux-firmware` | Bluetooth |
| `rtl_bt/rtl8821cs_config.bin` | **gerado pelo build** (não existe upstream) | ver F5 |

---

## 8. Correções prontas para portar (receitas)

Cada receita é independente. "Onde" indica a camada: DTS, kernel, rootfs ou usuário.

### F1 — Escolher o DTB certo no boot · *DTS/boot*

Sintoma: tela preta com backlight aceso, ou imagem mas controles errados.

```text
# /boot/extlinux/extlinux.conf
LABEL Ouvila
    KERNEL /Image
    FDT /dtbs/rk3566-powkiddy-x55.dtb      # X55  (X35S/X35H: -x35s.dtb)
    APPEND ... console=ttyS2,1500000n8 console=tty1 root=/dev/mmcblk1p2 rw
```

X55 com o DTB da X35S = driver de painel errado = tela preta. Foi exatamente a
regressão que originou este projeto.

### F2 — Desligar HDMI e `hdmi_sound` · *DTS*

Nenhuma das três placas tem porta HDMI. Com o HDMI habilitado, o Kodi em GBM pode
escolher esse conector "fantasma". E o `hdmi_sound`, sem codec, fica em *deferred
probe* eterno e atrapalha o áudio.

```dts
/* no fim do .dts da placa */
&hdmi {
	status = "disabled";
};
&hdmi_sound {
	status = "disabled";
};
```

### F3 — Wi-Fi: destravar o SDIO do RTL8821CS · *DTS*

Sem isto o probe falha com `sdio read32 failed ... -110` em série
(`probe with driver rtw_8821cs failed with error -110`) e o `wlan0` só aparece se você
recarregar o módulo na mão. Equivale ao patch `0004` da ROCKNIX.

```dts
&sdmmc1 {
	no-mmc;
	no-sd;
	sd-uhs-sdr50;
};
```

Depois disso: `mmc3: new ultra high speed SDR50 SDIO card`.

### F4 — Bluetooth: `dma-names` no `uart1` · *DTS*

Equivale ao patch `0021` da ROCKNIX. Sem isso o boot loga
`of_dma_request_slave_channel: dma-names property of node '/serial@fe650000' missing or
empty`.

```dts
&uart1 {
	dma-names = "tx", "rx";
};
```

### F5 — Bluetooth: o `config` que não existe upstream · *rootfs*

**Esta é a correção mais importante e a mais fácil de perder.** O `hci_h5` chama
`btrtl_get_uart_settings()`, que exige no `rtl8821cs_config.bin` uma entrada de
**offset `0x0c`** com baudrate e flow control. O `linux-firmware` **não publica** esse
arquivo; usar o `rtl8821c_config.bin` (10 bytes) renomeado faz o driver abortar com
`RTL: no UART config entry found` e o `hci0` fica com `BD Address 00:00:00:00:00:00`
("No default controller available").

Arquivo correto (25 bytes) — assinatura `0x8723ab55`, `total_len = 0x13`, uma entrada
`0x0c` de 16 bytes, baudrate `0x04928002` (1 500 000) e `data[12] = 0x5f`
(bit 2 = flow control, coerente com `uart-has-rtscts`):

```sh
printf '\x55\xab\x23\x87\x13\x00\x0c\x00\x10\x02\x80\x92\x04\x50\xc5\xea\x19\xe1\x1b\xfd\xaf\x5f\x01\xa4\x0b' \
  > /lib/firmware/rtl_bt/rtl8821cs_config.bin
# sha256: 6ddeb15f23588053e00cb08d25588bd7cf98d60fa93d9478efcef4ae8064a7ac
```

Resultado esperado no `dmesg`:

```text
Bluetooth: hci0: RTL: cfg_sz 25, total sz 36953
Bluetooth: hci0: RTL: fw version 0x75b8f098
```

E `hciconfig -a` mostra MAC real e `UP RUNNING`. Se aparecer
`Invalid header checksum` / `download fw command failed (-110)`, o arquivo está errado.
Detalhe que engana: os bytes do `config` são **anexados ao firmware** antes do download,
então o tamanho da entrada importa. Uma entrada de 13 bytes **passa** na validação de
`get_uart_settings` (`cfg_sz 22`) e depois corrompe o download. Use os 16 bytes
(`cfg_sz 25`).

### F6 — Áudio: habilitar o I2S · *kernel*

Ver a seção 6 (inclui o problema do `olddefconfig` e a asserção).

### F7 — Controles: driver e nó · *kernel + DTS*

1. Levar `rocknix-joypad.c`/`.h` para `drivers/input/joystick/`, registrar
   `CONFIG_JOYSTICK_ROCKNIX` no Kconfig e no Makefile.
2. Levar o adaptador `include/linux/input-polldev.h` (a API foi removida no 6.12).
3. O nó `rocknix-joypad` **na raiz** do device tree — nunca aninhado.

Sem isso: sem controle nenhum, ou controle "sem eixos".

### F8 — Rotação · *DTS + compositor*

X55: `display_transform=rotate-90` (Weston) e `rotation = <270>` no painel.
X35S/X35H: `display_transform=normal` e `rotation = <0>`.
Lembre: giram camadas diferentes (ver 3.1).

Nesta imagem o padrão é `display_transform=auto`, que escolhe pelo
`/proc/device-tree/model` (`*x55*` → `rotate-90`, resto → `normal`). Um valor explícito
sempre vence. Se você está portando, essa é a forma de uma imagem só servir nas três
placas.

### F9 — Weston cria `wayland-1`, não `wayland-0` · *usuário*

Weston 10.0.1, quando encontra `WAYLAND_DISPLAY` no ambiente, cria o socket como
`wayland-1`. O launcher do Kodi esperava `wayland-0`, dava timeout de 30 s e **matava um
Weston que estava funcionando**, caindo num fallback GBM que falhava porque o DRM ainda
estava preso. Correção:

```sh
weston --backend=drm-backend.so --socket=wayland-0 ...
# e, ao matar o Weston, esperar ~3 s antes de tentar GBM
```

Sintoma clássico: "o terminal aparece, o display funciona, mas o Kodi não abre / fica em
loop".

### F10 — Rede por USB (SSH sem Wi-Fi) · *kernel + rootfs*

```sh
modprobe g_ether            # cria usb0
ip addr add 10.55.0.1/24 dev usb0
ip link set usb0 up
```

Do PC: endereço **estático** `10.55.0.2/24` (não há DHCP no gadget). Para SSH do root
por senha no Debian 12, é preciso um drop-in, porque o padrão é
`PermitRootLogin prohibit-password`:

```ini
# /etc/ssh/sshd_config.d/10-ouvila.conf
PermitRootLogin yes
PasswordAuthentication yes
```

### F11 — rádio no Kodi · *usuário*

O Kodi **20 no Linux não tem UI de rádio** (o gerenciador de Wi-Fi do Kodi só existe nos
builds Windows/Android). Este projeto resolve com o add-on `script.ouvila.radio` e com
um painel web na porta 8088. Se você portar para outro sistema, precisa de um dos dois —
o Kodi não vai oferecer nada.

Atenção ao teclado, se for reimplementar o add-on: `xbmcgui.Dialog().input()` recebe o
tipo em `type` e a flag de ocultar em **`option`**. `type=64` (que é a flag sozinha)
**não abre o teclado**; `type=0, option=0` e `type=0, option=64` aceitam letras
(medido no aparelho).

---

### F12 — USB na X35S/X35H: **testado, não é o papel OTG** · *DTS / hardware*

Sintoma: o PC **não vê nada** (nem "dispositivo desconhecido") e o kernel loga

```text
dwc3 fcc00000.usb: failed to enable ep0out
```

O que **já foi descartado** (medido em 2026-10-24 numa X35H real):

| Tentativa | Resultado |
| --- | --- |
| `dr_mode = "peripheral"` no DTB | **não mudou nada**; UDC existe mas `state = not attached` |
| DTB da ROCKNIX e DTB compilado por nós | falham os dois |
| Conferir o DTB em uso | `dr_mode`, `phys`, `phy-names`, `extcon`, `otg-port` todos corretos |
| Comparar os DTBs **oficiais** das duas máquinas (release ROCKNIX 20261001) | nós `usb@fcc00000`, `usb@fd000000` e `usb2phy@fe8a0000` **idênticos**; só mudam os rótulos dos botões |
| `rk817-charger/online` | **0** no X35H contra **1** na X55 com o mesmo cabo |

`not attached` significa que o controlador **não detecta sessão/VBUS** no fio — não é
questão de papel. E o `failed to enable ep0out` é o DWC3 não conseguindo completar um
`DEPCMD` no endpoint 0, o que acontece quando o controlador/PHY não está operacional
para aquele papel.

**Hipótese atual:** a porta USB-C da X35H está no **outro** controlador. O RK3566 tem
dois e ambos sobem nesta imagem:

| Nó | Papel no DTS | PHYs | Serve para |
| --- | --- | --- | --- |
| `usb@fcc00000` | `otg` (vira device na X55) | `usb2phy0_otg` | gadget: rede USB, SSH |
| `usb@fd000000` | `host` | `usb2phy0_host` + `combphy1` (USB3) | pendrive, teclado, dongle |

Se a porta física da X35H estiver no segundo, ela é **host-only**: ligar num PC nunca
enumera (host contra host) e o gadget é **impossível por software**.

O ponto importante do log: a linha `failed to enable ep0out` sai de
`__dwc3_gadget_ep_enable()` dentro de `dwc3_gadget_start()` — isto é, o gadget aborta
**antes** de haver negociação com o host. Nenhum ajuste de papel, cabo ou `dr_mode`
pode contornar isso.

**Como decidir em 2 minutos** (console serial, `ttyS2` 1500000n8):

```sh
dmesg -w &                      # deixa rodando
# plugue um pendrive na porta (adaptador OTG se for USB-C)
ls /sys/bus/usb/devices/        # apareceu algo novo?
cat /sys/class/udc/*/state      # continua "not attached"?
```

* Apareceu dispositivo → porta **host**. Use pendrive, teclado e — para rede —
  um **adaptador USB-Ethernet** (isso resolve SSH na X35H).
* Nada apareceu → repita com o **mesmo cabo que funciona na X55** (controle) antes de
  concluir que a porta não tem dados.

O `dr_mode = "peripheral"` continua disponível como variante
(`extlinux.conf.x35s-peripheral`), mas **não** é o padrão: já foi testado e falhou.

## 9. Código que você precisa carregar junto (não está no mainline)

| Peça | De onde vem | Necessária para | Sem ela |
| --- | --- | --- | --- |
| `rocknix-joypad.c` / `.h` | ROCKNIX | controles de todas as três | sem gamepad / sem eixos |
| `input-polldev` shim | este repo (`build/`) | compilar o joypad no 6.12+ | build quebra |
| `panel-generic-dsi.c` | ROCKNIX | painel da **X35S/X35H** | painel não inicializa |
| `rk3566-powkiddy-x35s.dts` | ROCKNIX (patch) | X35S/X35H | U-Boot não acha FDT → tela preta |
| `rtl8821cs_config.bin` | **gerado** (F5) | Bluetooth da X55 | `hci0` sem endereço |

O DTS da X55, ao contrário, **é mainline** (existe desde a v6.8) — não precisa carregar
nada.

---

## 10. Sintoma → causa → correção

| Sintoma | Causa provável | Correção |
| --- | --- | --- |
| Tela preta, backlight aceso, terminal aparece | DTB da X35S numa X55 | **F1** |
| Kodi não abre / loop, display funciona | socket `wayland-1` do Weston | **F9** |
| Interface de ponta a cabeça | `display_transform` errado | **F8** |
| Imagem ~1,6% esticada na horizontal | `width_mm`/`height_mm` do painel | característica física |
| Sem placa de som (`parse error` no asoc) | I2S fora do kernel | **F6** |
| Sem som no alto-falante (jack ok) | `Playback Mux` em `SPK` | deixar em **`HP`** |
| `wlan0` não existe / `-110` no SDIO | falta `no-mmc`/`no-sd`/`sd-uhs-sdr50` | **F3** |
| `wlan0` existe mas `unavailable` | falta `wpa_supplicant` | instalar `wpasupplicant` |
| `hci0` com MAC zero, "No default controller" | falta o `config` com entrada `0x0c` | **F5** |
| `Invalid header checksum` no BT | `config` com entrada de 13 bytes | **F5** (16 bytes) |
| `dwc3 ... failed to enable ep0out` (X35S/X35H) | gadget aborta no controlador; UDC fica `not attached`; não é o papel OTG | **F12** (provável hardware) |
| `mmc3: Failed to initialize a non-removable card` (X35H) | SDIO do rádio não responde: sem Wi-Fi e sem BT | medir `pwrseq`/regulador no aparelho |
| `ssh root@...` recusa a senha certa | `PermitRootLogin prohibit-password` | **F10** |
| PC fica em `169.254.x.x` no cabo | gadget não faz DHCP | IP estático `10.55.0.2/24` |
| Sem controle | nó `rocknix-joypad` aninhado | **F7** |

---

## 11. Checklist para validar um aparelho novo

```sh
# 1. O DTB certo está no boot?
cat /proc/device-tree/model                       # "Powkiddy x55" / "Powkiddy x35s"

# 2. O painel subiu com o modo esperado?
cat /sys/class/drm/card0-DSI-1/status             # connected
cat /sys/class/drm/card0-DSI-1/modes              # 720x1280 (X55) / 640x480 (X35S)

# 3. Os controles existem?
dmesg | grep -i joypad                            # axes: 4, buttons: 17
ls /dev/input/event*

# 4. Tem placa de som?
cat /proc/asound/cards                            # card 0 [rk817ext]
aplay -l
amixer -c 0 sget "Playback Mux"                   # Item0: 'HP'

# 5. Wi-Fi
dmesg | grep -iE "mmc3|rtw88"                     # "SDR50 SDIO card"
ip link show wlan0

# 6. Bluetooth
dmesg | grep -iE "Bluetooth: hci0"
hciconfig -a                                      # MAC real + UP RUNNING

# 7. USB gadget
ls /sys/class/udc/                                # fcc00000.usb
ip addr show usb0                                 # 10.55.0.1/24
```

---

## 12. Limites conhecidos

* **X35S e X35H nunca foram testadas em hardware** por quem escreveu este documento.
  Tudo sobre elas aqui vem de fonte (DTS/driver da ROCKNIX) e de leitura de código,
  não de medição.
* **Vídeo por hardware:** o `hantro_vpu` registra `/dev/video0` (dec) e `/dev/video1`
  (enc), mas não há driver VA-API do Rockchip no rootfs (`va_openDriver() returns -1`).
  Alternativa: V4L2/DRM PRIME direto.
* **Cores de 32 kHz do `rk817`** entram na power sequence do SDIO; se você portar para
  uma árvore sem o `rk817` completo, o Wi-Fi pode não inicializar.
* O documento descreve a **mainline v6.12**. Em kernels mais novos alguns patches
  (por exemplo o do `dma-names`) já podem estar aplicados — confira antes de duplicar.
