# Build

## Fluxo completo

O workflow em `.github/workflows/build.yml` continua responsável por baixar as fontes, compilar U-Boot/kernel, montar o rootfs Debian e produzir **uma imagem única para os três aparelhos** (X55, X35S e X35H; X35S e X35H compartilham o mesmo DTB). O boot ativo usa o **DTB da X55** (aparelho em teste). O DTB da X35S/X35H — oficial obtido da ROCKNIX quando disponível, senão o compilado — fica em `extlinux/extlinux.conf.x35s`. Se o DTB ativo (X55) não existir, o job falha explicitamente em vez de criar uma imagem com FDT ausente; a ausência do DTB da X35S/X35H apenas avisa. Isso não declara validação em hardware.

## X55: diagnóstico de tela preta

O diagnóstico enviado após o teste no X55 identifica o painel MIPI-DSI e o modo 720x1280, e o Kodi informa que escolheu `DSI-1` (não HDMI). Ao mesmo tempo, o kernel registra `Failed to register component: -517` no host DSI e repetidos `POST_BUF_EMPTY irq err at vp1`. Portanto, o log não confirma que o Kodi selecionou HDMI como saída; aponta uma falha na apresentação pelo caminho DSI/VOP.

Como teste do conflito conhecido entre HDMI e o display interno, o workflow agora compila o DTB X55 com o nó HDMI desabilitado, seguindo o workaround já usado para o X35S. Isso remove a saída HDMI externa enquanto o workaround estiver ativo. A hipótese só pode ser confirmada repetindo o teste no aparelho com a nova imagem; se o painel continuar preto, o erro do caminho DSI/VOP requer investigação separada.

## X55: orientação e controle

O diagnóstico posterior enumera apenas os dispositivos de power e volume, pois o
botão não está sendo registrado no kernel. A solução de controle usada pela
comunidade ROCKNIX é o driver `rocknix-joypad`, não a combinação genérica
`gpio-keys`/`adc-joystick` testada anteriormente. O build agora mantém o binding
ROCKNIX para combinar GPIOs e ADCs numa única entrada de gamepad, e adapta o
uso legado de `input-polldev` à API de polling disponível no Linux 6.12. Também
normaliza o DTS para conter somente um nó `rocknix-joypad`.

O nó precisa ser filho direto da raiz do device tree porque o driver é um
`platform_driver`. O diagnóstico da primeira imagem ROCKNIX mostrou que um nó
aninhado sob o controlador DSI não é registrado como dispositivo de entrada;
o workflow agora verifica que o nó está na raiz antes de compilar o DTB.
Após corrigir o DTB no X55 conectado, o kernel registrou `retrogame_joypad`
com 4 eixos e 17 botões, e o Kodi registrou o joystick. A captura no aparelho
confirmou eventos de pressionar/soltar nos quatro sentidos do D-pad e nos
controles físicos mapeados; os dois analógicos percorreram seus quatro eixos
até quase o curso completo. Como o popup “New controller detected” aparecia
sem permitir navegar, foi adicionado um mapa Kodi específico para
`retrogame_joypad`. D-pad para cima e A foram confirmados na interface após
instalá-lo. A GPIO fantasma `BTN_MODE` não é mapeada para uma ação, pois fica
ativa em repouso.

O mapa em
`variants/kodi/overlay/usr/share/kodi/addons/peripheral.joystick/resources/buttonmaps/xml/linux/retrogame_joypad_17b_4a.xml`
define os 17 botões e 4 eixos na ordem observada no aparelho; o workflow o
instala no rootfs após instalar os pacotes do Kodi. A disposição física segue o
Nintendo Switch Pro Controller: A à direita, B abaixo, X acima e Y à esquerda.

As imagens X55 incluem o marcador `ouvila_x55_gamepad=rocknix-v1` na linha de
boot. O diagnóstico registra `/proc/cmdline` e os nós ativos do device tree:
isso permite distinguir uma falha de probe do driver de uma inicialização
acidental pela imagem/DTB anterior.

O painel declara orientação DRM `left-up`, que o console aplica, mas o Kodi em
GBM direto escolhe o modo DSI nativo 720x1280 sem transformar a superfície. Para
girar somente a interface Kodi, a sessão inicia Weston no backend DRM, com shell
kiosk, e executa Kodi como cliente Wayland. A rotação da saída `DSI-1` não fica
fixa no `weston.ini`: `start-kodi-wayland.sh` lê `display_transform=<valor>` de
`/boot/ouvila.conf` (partição FAT, editável no PC), valida contra os oito
valores aceitos pelo Weston (`normal`, `rotate-90`, `rotate-180`, `rotate-270` e
as variantes `flipped-*`) e gera o `weston.ini` efetivo em `$XDG_RUNTIME_DIR`;
valor ausente ou inválido cai em `normal`, com aviso no journal. O valor
histórico `transform=270` era inválido (o Weston aceita `rotate-270`, não `270`)
e foi retirado. O terminal permanece sob a orientação existente do framebuffer.
Como isso adiciona uma etapa de composição, a orientação e a reprodução de vídeo
acelerada precisam ser confirmadas no aparelho antes de considerar a rotação
concluída. O sentido correto no X55 foi **validado no aparelho: `rotate-90`**;
a imagem grava `rotate-90` para o X55 (`display_transform` em
`devices/powkiddy-x55/device.conf`, no `create-image.sh`) e `normal` para X35S/X35H.

O primeiro diagnóstico da v6 mostra `kodi.service` encerrando com status 1
antes da criação de `kodi.log`; portanto, não confirma que o Kodi chegou a
iniciar. O `weston.log` revelou `unknown backend "drm"`: nesta versão o Weston
espera o nome do módulo `drm-backend.so`, e não o alias `drm`. O launcher foi
corrigido para usar esse nome. O script de diagnóstico inclui o log Weston, os
nós DRM e o estado completo do serviço; erros do compositor também são
copiados para o journal. Reinicializações repetidamente malsucedidas são
limitadas.

## USB gadget (SSH pelo cabo): funciona no X55, falha na X35S/X35H

O gadget ethernet (`g_ether`) funciona na X55: o PC enxerga a `usb0` e o SSH
responde pelo cabo. Na X35H (imagem com o DTB da X35S) o kernel registra
`dwc3 fcc00000.usb: failed to enable ep0out` e o PC não vê nada.

O controlador OTG é o `usb_host0_xhci` (`usb@fcc00000`), um DWC3. No SoC base
(`rk356x-base.dtsi`) ele sai com `dr_mode = "otg"`, e nenhum DTS de placa
altera esse valor: o mainline da X55 e os patches da ROCKNIX (reescrita do X55 e
criação do X35S) não tocam em USB. O DTS da X35S da ROCKNIX é
`#include "rk3566-powkiddy-x55.dts"` e só sobrescreve `model`, `battery`,
`joypad` e `panel`.

Até esta rodada o workflow **forçava `dr_mode = "peripheral"` apenas nos DTBs da
X35S** (o compilado e o oficial da ROCKNIX), o que era a única diferença de USB
entre a imagem que funciona (X55, `otg`) e a que falha (X35H). O forcing foi
removido; os DTBs ficam como vieram (`otg`), igual ao X55.

Para testar o outro cenário sem recompilar, a imagem inclui um DTB alternativo
com `dr_mode = "peripheral"` e uma entrada de boot correspondente: no PC
(partição FAT), copie `extlinux/extlinux.conf.x35s-peripheral` por cima de
`extlinux/extlinux.conf`. O padrão continua sendo `otg`.

O diagnóstico (`diagnostico.txt`) passou a registrar o `dr_mode` em uso
(`/proc/device-tree/usb@fcc00000/dr_mode`), o estado dos UDC, os papéis
(`usb_role`) e o `dmesg` filtrado em `dwc3`/`ep0`. É **hipótese** que remover o
`peripheral` resolva; se `otg` também falhar na X35H, a causa passa a ser
hardware (VBUS/ID da porta) ou o PHY. O script do diagnóstico vem de
`variants/kodi/overlay/usr/local/bin/dump-diagnostico.sh` (o CI o instala com
`install`, sem cópia inline no `build.yml`).

## Publicação no GitHub Releases

O workflow publica a imagem **no próprio Release**, porque o artifact do Actions expira
(90 dias) e exige login para baixar. O arquivo é exatamente o mesmo
`rk3566-x55-*-sdcard.img.gz` — **não é um ISO9660**: o U-Boot é gravado nos setores 64
e 16384 do disco, então um ISO comum não inicializa o aparelho.

| Gatilho | O que acontece |
| --- | --- |
| tag `v*` (ex.: `v1.0`) | release com o nome da tag, variante `kodi`, asset da imagem + `.sha256` |
| tag `retroarch-*` (ex.: `retroarch-v1.0`) | release com o nome da tag, variante `retroarch` |
| push na `main` | release rolante **`latest`** (marcado *pre-release*), assets sobrescritos a cada build |

Se um release da tag já existir, as notas e os assets são atualizados em vez de falhar
(`gh release upload --clobber`), então recriar uma tag é seguro. O checksum é publicado
por arquivo (`<imagem>.img.gz.sha256`), para que as duas variantes convivam no `latest`
sem uma sobrescrever o checksum da outra. O job precisa de `permissions: contents: write`
para usar a API de releases.

Para publicar uma versão:

```sh
git tag v1.0
git push origin v1.0
```

Depois baixe em **Releases** (ou em `releases/download/v1.0/...`), confira com
`sha256sum -c rk3566-x55-mainline-sdcard.img.gz.sha256` e grave com balenaEtcher,
Rufus (modo DD) ou `gunzip -c imagem.img.gz | sudo dd of=/dev/sdX bs=4M`.

## Instalar no eMMC (as três mídias)

A imagem é a mesma para **eMMC, SD1 e SD2** — quem resolve é o `root=PARTUUID` gravado
no `extlinux.conf` (não há mais `/dev/mmcblk1p2` fixo), e o U-Boot varre
`mmc1 → mmc0 → mmc2` (SD1, eMMC, SD2).

| Mídia | Dispositivo | Tamanho medido | Observação |
| --- | --- | --- | --- |
| eMMC (`mmc@fe310000`) | `mmcblk0` | **3776 MiB** | é o limite de tamanho da imagem |
| SD1 (`mmc@fe2b0000`) | `mmcblk1` | o cartão | slot usado para testar |
| SD2 (`mmc@fe000000`) | `mmcblk2` | o cartão | só inicia se os outros não tiverem sistema |

O padrão de tamanho da imagem é **3584 MiB** justamente para caber no eMMC.
Para cartões maiores, `OUVILA_IMAGE_SIZE_MB=8192 build/build.sh ...`.

**Gravar no eMMC a partir do aparelho** (com o sistema rodando do SD):

```sh
# copie a imagem para a partição FAT (/boot) do cartão, ou baixe por Wi-Fi
/usr/local/bin/install-image-to-emmc.sh /boot/rk3566-x55-retroarch-sdcard.img.gz
# ele confere o tamanho, zera a GPT antiga (o ROCKNIX de fábrica usa GPT e a
# nossa imagem é MBR), escreve e mostra o resultado
```

Depois: desligar, **remover o cartão SD** e ligar. O U-Boot tenta SD1 primeiro; sem
cartão, inicia o eMMC.

Cuidados que já custaram erro:

* **Nunca `dd` uma imagem maior que o disco.** Termina em `No space left on device`
  com instalação truncada (não inicia) e não há aviso depois.
* **Zere a GPT antes** de escrever uma imagem MBR; senão o kernel vê a GPT de backup no
  fim do disco e reclama `GPT PMBR size mismatch`.
* Gravar no eMMC **apaga o ROCKNIX de fábrica** (não há partição de recuperação).

## Empacotamento local

`build/build.sh <device> <variant>` empacota artefatos já preparados. Por exemplo:

```sh
build/build.sh powkiddy-x55 kodi
build/build.sh powkiddy-x55 retroarch
```

`kodi` é a variante de multimídia e `retroarch` a de jogos; o que muda entre elas
(pacotes, serviço do tty1, artefato) está em [variantes.md](variantes.md).

O script espera por `uboot/idbloader.img`, `uboot/u-boot.itb`, `kernel/arch/arm64/boot/Image`, o DTB indicado em `devices/<device>/device.conf` e uma árvore `rootfs/` que contenha o sistema da variante. `variant.conf` declara `status` e o caminho relativo do manifesto de pacotes; variantes planejadas sem implementação não podem ser empacotadas. É possível substituir os diretórios com `OUVILA_UBOOT_DIR`, `OUVILA_KERNEL_DIR`, `OUVILA_ROOTFS_DIR`, `OUVILA_OUTPUT_DIR` e o DTB com `OUVILA_DTB_SOURCE`. Caminhos relativos são resolvidos a partir da raiz do repositório. A criação de partições exige `sudo` e as ferramentas `dd`, `parted`, `losetup`, `mkfs.vfat`, `mkfs.ext4` e `gzip`.

As imagens não são sobrescritas: escolha um nome novo com `OUVILA_IMAGE_NAME` ou remova manualmente um artefato anterior antes de repetir o comando.
