# Estado do projeto e retomada

Ponto de partida para continuar o trabalho depois de testar no aparelho. Diz o que já
está pronto, o que ainda é **hipótese** e exatamente o que testar e devolver.

## PONTO DE RETOMADA (2026-11-06) — R35S adicionado a partir do TTL

> Leia esta seção antes de qualquer coisa. O resto do documento é o histórico.

### R35S: hardware extraído do aparelho por console serial

O usuário ligou um adaptador **TTL** no R35S e pediu para tirar do aparelho tudo o que
falta para adicioná-lo, ignorando o sistema que estava nele. O que se aproveita não é o
sistema: é o **device tree vivo** e o que os subsistemas respondem. Tudo está em
[r35s-rk3326.md](r35s-rk3326.md) §12 — resumo:

- **a R35S é a R36S**: `model = Game Console R36S`, `compatible` com
  `gameconsole,r35s` + `gameconsole,r36s` + `rockchip,rk3326`;
- **o nosso overlay de painel está correto**: 160 entradas, 159 idênticas ao painel real
  (a única diferença é um espaço duplo) e `reset-gpios` igual;
- **analógico em −1800..+1800** (não 0..1023 do RK3566) → corrigido no app, que agora lê
  o `ABSINFO`; sem isso o R35S rolava a lista sozinho;
- **USB é `dwc2` em peripheral e o UDC existe** → gadget/SSH deve funcionar (≠ X35H);
- **dois slots SD, os dois com card-detect**, eMMC desligado;
- **áudio `Speaker → SPKO`** (≠ X55, que usa HPOL/HPOR);
- **sem Wi-Fi/BT**;
- **sem `KEY_POWER`** (o RK817 não tem nó `pwrkey`): os gestos de power do app não agem — use menu (SELECT) → Apagar a tela / Desligar, e o PMIC para o desligamento por hardware.

Ferramentas novas: [`tools/ttl-extract.py`](../tools/ttl-extract.py) (extração completa
pelo serial, só leitura) e [`tools/fdt-dump.py`](../tools/fdt-dump.py) (lê `.dtb` sem
`dtc`). O DTB da unidade ficou em
[`devices/game-console-r35s/dts/referencia/`](../devices/game-console-r35s/dts/referencia/Readme.md).

**O que falta:** a imagem do R35S **nunca foi bootada** (o loader mainline falhou em
2026-10-30, §11 daquele doc). O pipeline agora aceita `variant=media-player` na linha
RK3326, então o próximo passo é *Run workflow* → `target=imagem`, `loader=oficial`,
`variant=media-player` e gravar. Com o console serial ligado, qualquer falha de boot é
visível na hora — e o UDC permite SSH em **10.55.0.1** se o gadget subir.

## PONTO DE RETOMADA (2026-11-01, v0.3)

> Leia esta seção antes de qualquer coisa. O resto do documento é o histórico.

### Variante `media-player` v0.3: a expansão pedida pelo usuário

Depois do primeiro teste no aparelho o usuário pediu um pacote grande de mudanças.
Tudo foi implementado **offline** (compila para arm64 contra o bookworm, 43 casos de
selftest, as 4 abas + menu + 10 idiomas + 5 temas desenham). **Nada disto foi testado
no aparelho ainda** — é o que a tag `media-player-v0.3` deve levar para o cartão.

O que mudou:

1. **Wi-Fi e Bluetooth saíram da aba Rádio e foram para o menu** (submenus com lista de
   redes/dispositivos). A aba Rádio **deixou de existir** — estes aparelhos não têm
   AM/FM. O lugar dela na barra virou a **Biblioteca**.
2. **Aba Biblioteca**: navegação por **álbum** (lista de álbuns → faixas do álbum),
   como o usuário pediu ("na lista da biblioteca tem que ser navegável para escolher o
   álbum").
3. **Modos de reprodução** no quinto botão dos controles: em ordem, repetir álbum,
   repetir faixa e aleatória. Gravado em `/boot/ouvila-media.conf`.
4. **Equalizador de 10 bandas** no menu → Som, com 8 predefinições, pré-amplificação e
   limitador. **Melhoria de som (modo neural)** aparece **desativada** porque o mainline
   não tem `/dev/rknpu` — era o pedido explícito ("em caso de não disponibilidade o
   botão não poderá ser ativado").
5. **10 idiomas** (pt, en, zh, hi, es, ar, bn, id, ur, fr) com HarfBuzz para juntar as
   letras do árabe/urdu e reordenar as vogais do hindi/bengali, e fontes Noto/WenQuanYi
   como reserva por alfabeto.
6. **Relógio** ajustável no menu (ano/mês/dia/hora/minuto + aplicar).
7. **5 temas** (os protótipos de `variants/media-player/layout`) como dados; nenhuma cor
   fixa no código da UI.

**Imagem para gravar: `media-player-v0.4`** (204,6 MB — cresceu de 177 MB porque agora
leva `fonts-noto-core`, `fonts-wqy-zenhei` e `libharfbuzz0b`). Ela é a v0.3 **mais** dois
ajustes que ficaram de fora daquela tag:

1. **linha de álbum mais alta na Biblioteca** — nome e artista do álbum se sobrepunham
   na lista (é a tela que o usuário mais vai olhar, então valia uma tag nova);
2. **guarda no equalizador quando não há taxa de amostragem** (saída de áudio desligada):
   os coeficientes eram calculados com `rate = 0` e uma banda salva com ganho != 0
   produziria `NaN` nas amostras.

A v0.3 (`41303887981`) também passou com todos os passos verdes; a v0.4 é a mesma coisa
com os dois ajustes acima — run `41306623069`, **todos os passos verdes**, release
publicada em 2026-11-01.

**O que testar com a imagem nova (v0.3):**

- **power** curto (apaga/acende) e 3 s (diálogo de desligar, foco começa em Cancelar);
- **menu do SELECT**: brilho (piso de 10%), Wi-Fi, Bluetooth, Som, Idioma, Relógio,
  Tema, Apagar a tela, Desligar, Reiniciar;
- **equalizador** audível (mudar o preset Graves/Voz deve ser óbvio) e sem distorção;
- **idiomas** exóticos de verdade na tela: árabe e urdu ligados, hindi e bengali com as
  vogais no lugar, chinês sem quadradinho (é o que só a tela pode confirmar);
- **relógio** aplicando (desligar o aparelho, esperar e ver se o horário ficou);
- **5 temas** trocando na hora;
- **biblioteca**: escolher álbum, tocar faixa, B voltar para a lista de álbuns;
- **modos**: repetir faixa e aleatório no fim de uma música;
- no X35H, Wi-Fi/BT devem aparecer como **"Ausente"** (o SDIO do rádio não responde
  lá) — isso não é bug do app.

### Variante `media-player`: primeiro teste no aparelho (2026-11-01)

O usuário gravou a `media-player-v0.1` na **X35H** e o app **subiu, desenhou em tela
cheia e tocou música** — a arquitetura "sem camada" (DRM/KMS + FFmpeg + ALSA num
binário só) funciona no hardware. Os problemas eram de controle e de fim de faixa:

1. o app **parava no fim de cada música** (o log mostrava "terminou (interrompido)" a
   cada 1-2 s) — agora abre a próxima da lista; no vídeo volta para a grade;
2. **ligar/desligar** era na aba Sistema → foi para o **botão de power** (toque curto
   apaga/acende a tela com DPMS off, 3 s abre a confirmação de desligar). Exige o
   drop-in `logind.conf.d/10-ouvila-media-power.conf` (`HandlePowerKey=ignore`),
   senão o systemd desliga o aparelho antes do app ver a tecla;
3. a aba Sistema virou **só informação** e todo ajuste foi para o **menu do SELECT** —
   era ali que o botão de volume se confundia com o brilho;
4. **brilho com piso de 10%** + "apagar a tela" para economizar bateria;
5. **L1/R1** saltam faixa/página e dão ±30 s no vídeo.

**O que testar com a imagem nova (v0.2):** power curto/longo, DPMS no painel, vídeo em
tela cheia com L1/R1, e se há silêncio entre faixas. Detalhes e o que já foi validado
em [variantes.md](variantes.md#o-que-já-foi-validado-no-aparelho-e-o-que-não-foi).

## PONTO DE RETOMADA (2026-10-30)

> Leia esta seção antes de qualquer coisa. O resto do documento é o histórico.

### O que mudou nesta sessão

1. **Análise da pasta** (pedido: "analise a pasta e entenda o status"): o estado
   descrito abaixo (imagens #119/#120, X55 e X35H validados; X35H sem rádio e com
   dúvida de USB) **continua sendo o do projeto**. Nada no CI mudou nesta sessão —
   o workflow não foi tocado de propósito (push na `main` gera build).
2. **Placa nova preparada: `devices/game-console-r35s/`** — e ela **não é RK3566**.
   É **Rockchip RK3326** (família PX30). O diretório já existia com dois DTS
   **decompilados** de kernel de fabricante (o painel completo) e um
   `device.config` inválido (conteúdo `1`). Ficou assim:
   - `device.conf` de verdade, com `image_dtb=` **vazio** e
     `status=reference-rk3326-needs-separate-pipeline` → o empacotador **recusa**
     a placa em vez de gerar imagem que não liga;
   - `kernel.fragment`, `dts/Readme.md` e `uboot/README.md` com os dados
     confirmados e o que falta medir;
   - **overlay de painel gerado e validado** (`dts/panel-overlay/`) + a ferramenta
     `tools/panel_overlay_from_dts.py`.
3. **Detalhe, riscos e checklist do RK3326:** [`r35s-rk3326.md`](r35s-rk3326.md).

### R35S/R36S: o painel (a parte que "escaneia" não existe)

O R35S sai de fábrica com **vários modelos de tela** e cada um precisa da sua
sequência de inicialização. Procurei o método de "escaneia e acha o modelo" na
KNULLI: **não existe**. A KNULLI usa kernel 4.4 de fabricante com o DTB **fixo**
`rk3326-rg351mp-linux.dtb`; a ROCKNIX/REG Linux usa **overlay no cartão**
(`overlays/mipi-panel.dtbo`, variantes `r36s-panel1..4` / `r36s_clone` / …) e a
documentação manda *"descubra qual você tem, ou vá trocando até funcionar"*.
Painel DSI não tem EDID; sem driver de painel funcionando não há ID para ler.

O que foi feito: converter o `mipis-panel4.dts` (que já estava na pasta) para o
overlay `rocknix,generic-dsi` e **conferir contra o arquivo oficial**
`mipi-panel.dtbo.r36s-panel4` → `--check-arquivo` dá **160 strings idênticas**.
Três armadilhas que essa comparação revelou (e que estão documentadas em
[`hardware-e-drivers.md`](hardware-e-drivers.md) §4.1):

* `flags` do fabricante (`0xa03`) precisa do bit `0x400` → **`0xe03`**;
* `horizontal`/`vertical` são **(display, front-porch, sync-len, back-porch)** —
  a documentação anterior descrevia como hsync_start/htotal (estava ambíguo);
* nós "50Hz"/"75Hz" do fabricante vêm com porches copiados do 60 Hz (77,4 fps);
  o overlay oficial recalcula o `vfront-porch` para bater no nome.

### Próximos passos, na ordem

1. **R35S: medir no aparelho com a imagem oficial** (ROCKNIX/ArkOS) antes de
   escrever DTS próprio: `cat /proc/device-tree/model`, qual DTB ela usa,
   `evtest` (botões), `/sys/bus/iio/devices` (ADC), `lsblk` (TF1/TF2),
   `ls /sys/class/udc` (USB), `rfkill list` (rádio). Isso decide o DTB base e
   evita chutar `gpio-keys`. Depois: job RK3326 no CI (checklist em
   [`r35s-rk3326.md`](r35s-rk3326.md) §5).
2. **Testar um jogo de verdade** na imagem #119 (ROM em `/roms/<sistema>/`, PS1
   exige BIOS em `/storage/bios`). É o único item da variante de jogos que falta.
3. **X35H — teste do pendrive** (2 min): plugar e ver `ls /sys/bus/usb/devices/`.
   Apareceu algo → porta é **host** e um adaptador USB-Ethernet dá rede/SSH; nada
   → porta só de carga.
4. **X35H — rádio** (sem Wi-Fi e sem BT): `mmc3: Failed to initialize a
   non-removable card`. Precisa medir `pwrseq`/regulador no aparelho.
5. Longo prazo (não bloqueia): VA-API, senha/SSH do root, CI chamar
   `build/create-image.sh`, mover scripts compartilhados de
   `variants/kodi/overlay/` para `common/overlay/`.

---

## PONTO DE RETOMADA (2026-10-25)

> Seção anterior, mantida como histórico: o estado das duas variantes e das
> pendências do X35H continua válido.

### Próximos passos, na ordem

1. **Testar um jogo de verdade** na imagem #119 (ROM em `/roms/<sistema>/`, PS1 exige
   BIOS em `/storage/bios`). É o único item da variante de jogos que falta.
2. **X35H — teste do pendrive** (2 min, pendrive ainda não plugado): plugar e ver
   `ls /sys/bus/usb/devices/`. Decisão: apareceu algo → porta é **host** e um adaptador
   USB-Ethernet dá rede/SSH; nada → porta só de carga.
3. **X35H — rádio** (sem Wi-Fi e sem BT): `mmc3: Failed to initialize a non-removable
   card`. Precisa medir `pwrseq`/regulador no aparelho; não é ajuste de software.
4. Longo prazo (não bloqueia): VA-API (`va_openDriver() returns -1`), senha/SSH do root,
   CI chamar `build/create-image.sh`, mover scripts compartilhados de
   `variants/kodi/overlay/` para `common/overlay/`.

### Tela de boot

A imagem liga com o **logo do Ouvila** na tela (Plymouth, tema `ouvila`), em vez de
texto do kernel. A arte tem versão paisagem e retrato e o tema escolhe pelo tamanho real
da tela (o Plymouth desenha antes da rotação do Weston). Detalhes e o escape (remover
`splash` do `extlinux.conf`) em [variantes.md](variantes.md#tela-de-boot-plymouth-e-ícone-de-cada-variante).

**Ainda não visto no aparelho** — o primeiro teste precisa confirmar quanto tempo a
splash fica na tela e se a troca para o app não pisca.

### As imagens

| Variante | Build | Artefato | Tamanho | Estado |
| --- | --- | --- | --- | --- |
| `kodi` (multimídia) | **#120** | `rk3566-x55-mainline-sdcard-image` | ~356 MB | validada em hardware; #120 acrescenta o `dmesg` de USB no `usb-watch` |
| `retroarch` (jogos) | **#119** (tag `retroarch-v0.9`) | `rk3566-x55-retroarch-sdcard-image` | 536 MB | **use esta para jogos**: tela, rotação, áudio e controles validados no X55 e no X35H |

Pegue em **Actions → run #120 / #119 → Artifacts** ou em **Releases** (cada push na
`main` atualiza a release rolante `latest`; tags `v*`/`retroarch-*` criam um release da
versão com a imagem + `.sha256`). São duas imagens completas: grave em cartões separados
(não dá para trocar de variante pelo cartão).

O que **entrou** em #119 (e não estava em nenhuma imagem antes):
`video_fullscreen_x/y` com o tamanho já girado, perfil de controle
`Rocknix_Gamepad.cfg`, e o critério de "sessão abaixo de 10 s não é sucesso".

### Armazenamento: 1 eMMC + 2 slots SD (2026-10-28, medido no aparelho)

Antes desta sessão a imagem assumia **um** disco e o chamava de `/dev/mmcblk1p2`. No
aparelho existem três mídias, e o sistema pode estar em qualquer uma:

| Mídia | Alias DT / controladora | Dispositivo | Tamanho |
| --- | --- | --- | --- |
| eMMC | `mmc0` / `mmc@fe310000` (sdhci-dwcmshc) | `mmcblk0` | **3776 MiB** |
| SD1 | `mmc1` / `mmc@fe2b0000` (dwmmc) | `mmcblk1` | o cartão |
| SD2 | `mmc2` / `mmc@fe000000` (dwmmc) | `mmcblk2` | o cartão |

O que mudou (build novo necessário):

* **`root=PARTUUID=<do build>`** no `extlinux.conf` em vez de `/dev/mmcblk1p2`.
  **Validado no aparelho** (reboot real): `VFS: Mounted root (ext4 filesystem) on
  device 179:130` com `root=PARTUUID=40d50178-02`.
* **U-Boot varre o SD2**: `BOOT_TARGETS "mmc1 mmc0 mmc2 ..."` (o mainline só tinha
  `mmc1 mmc0`, então sistema no 2º slot nunca iniciava). Ordem escolhida: SD1 → eMMC →
  SD2.
* **Imagem 3584 MiB** (era 4096): não cabia no eMMC de 3776 MiB.
* **`ouvila-extra-storage.service`**: monta os slots que não têm o sistema em
  `/storage/sdcard<N>` e liga em `/boot/roms/sdcard<N>` (aparece no RetroArch). Detecta
  cartão inserido depois do boot religando o slot (`unbind`/`bind`) — necessário porque
  os slots não têm `cd-gpios`. Testado no aparelho: montou, ligou, desmontou ao sumir o
  dispositivo e remontou após o rescan.
* **`tools/install-image-to-emmc.sh`**: grava a imagem no eMMC com conferência de
  tamanho e limpeza da GPT antiga.

> Ressalva honesta: o boot a partir do **eMMC** e do **SD2** não foi testado (exigiria
> sobrescrever o ROCKNIX de fábrica). O que está provado é o `root=PARTUUID` (que é
> independente da mídia) e a detecção/montagem dos dois slots.

### Variante de jogos: o que já foi validado (X55 e X35H)

O primeiro boot da imagem de jogos **não** abria: o RetroArch era morto pelo Weston
em 7,5 s (violação de protocolo) e nenhum botão respondia. Os dois bugs foram
corrigidos e **confirmados no aparelho**:

| Item | Estado |
| --- | --- |
| Boot / serviço no tty1 | OK — `retroarch.service` ativo, sem reinício em laço |
| Tela 1280x720 girada (`rotate-90`) | **OK** (menu na horizontal) |
| Áudio | OK — `/dev/snd/pcmC0D0p` aberto pelo RetroArch, mux em `HP` |
| Botões, D-pad, L/R/L2/R2, L3/R3, F | **OK** (teste do usuário) |
| Analógicos (os dois) | **OK** (teste do usuário) |
| X35H (mesmo cartão): 640x480, `normal` | **OK** — sessão de 81 s (antes morria em 7,5 s) |
| Jogo de verdade (carregar ROM) | **não testado** — próximo passo |
| Sair pelo menu **desliga** o aparelho | **CONFIRMADO no aparelho (2026-10-28)**: com o launcher novo instalado no X35H, "Quit RetroArch" no menu → tela "Desligando..." → **o aparelho desligou de verdade** (o gadget USB sumiu do PC e a rede caiu, não foi queda de rede). Antes, fechar só matava o processo e deixava o aparelho ligado, sem interface no `tty1`. `retroarch_exit=stay` em `/boot/ouvila.conf` volta ao comportamento antigo |

As duas correções **já estão na imagem #119**. Se for gerar outra no futuro, o caminho
é `git tag retroarch-vN && git push origin retroarch-vN` (push na `main` gera `kodi`).
Cuidado: a marca de skip do CI vale no **corpo** da mensagem — documentá-la cancela o
build (custou 3 tags silenciosas nesta sessão).

### Se algo falhar, onde olhar

1. `/boot/retroarch-start.log` — qual driver foi tentado, em que tamanho, e a duração
   da sessão (sessão abaixo de 10 s é tratada como falha, não como saída do usuário).
2. `/boot/diagnostico.txt` — DRM, áudio, entrada, rotação efetiva, cfg do RetroArch e
   a lista de cores instalados.
3. Console serial (`ttyS2`, autologin root) — o `tty1` pertence ao app.

### Pendências do X35H: USB e rádio (sem Wi-Fi e sem BT)

**USB:** a falha é do **controlador**, não do cabo nem do papel. O `dmesg` do X35H tem,
e o da X55 não tem:

```
dwc3 fcc00000.usb: failed to enable ep0out
```

Isso é `__dwc3_gadget_ep_enable()` abortando dentro de `dwc3_gadget_start()`: o gadget
não inicializa, o UDC nunca vai a `configured` (fica `not attached`) e `usb0` fica
`NO-CARRIER`. Descartei device tree de vez — comparei os DTBs **oficiais** dos dois
aparelhos (release ROCKNIX 20261001) e os nós USB/PHY são idênticos. O
`rk817-charger/online` é `0` no X35H contra `1` na X55.

**Teste que decide:** plugar um pendrive e ver `ls /sys/bus/usb/devices/`. Se aparecer,
a porta é **host** (adaptador USB-Ethernet resolve rede/SSH). Se nada aparecer em papel
nenhum, é porta só de carga. Use o mesmo cabo que funciona na X55 como controle.

**Rádio:** `mmc3: Failed to initialize a non-removable card` — o SDIO do rádio não
responde, então `wlan0` não existe e `hci0` fica sem endereço (`radio-retry` tenta
`rtw88_8821cs` três vezes e desiste). Também é independente de software: os nós de mmc
são idênticos aos da X55.

### Armadilhas que custaram build (não repita)

1. Validar pacote por `packages.debian.org/<suite>/arm64/<pkg>`: responde **HTTP 200**
   com página de erro quando o pacote não existe para arm64. Use
   `dists/bookworm/main/binary-arm64/Packages.gz`.
2. `rootfs/root` é **0700**: escrever **e ler** exige `sudo`.
3. `[skip ci]` no commit de topo cancela o build da **tag** que aponta para ele.
4. `:` dentro de `name:` de um step no YAML quebra o parse.

## Como retomar (histórico)

> **Estado em 2026-10-24 (use o #100, 356 MB):** junta tudo o que foi validado e o que
> é novo: X55 ativo com **`rotate-90`** (validado no aparelho), SSH `root/root`, Kodi
> com `--socket=wayland-0` (2 h sem reiniciar), **áudio I2S**, **Wi-Fi/BT** (firmware +
> `wpa_supplicant` + `bluez`) e o **painel web em `http://10.55.0.1:8088`** para
> gerenciar rádio. Os #90/#91 falharam por drift das fontes externas — o CI agora fixa
> U-Boot/ROCKNIX/rkbin por commit.

1. Pegue o artifact `rk3566-x55-mainline-sdcard-image` do **run #100 (success)**
   em Actions. Ele traz: boot ativo da X55 (`display_transform=rotate-90`),
   **SSH com senha liberado**, Kodi com `--socket=wayland-0`, áudio (I2S) habilitado,
   **Bluetooth funcionando** (config UART gerada no build), o **add-on Ouvila Radio**
   dentro do Kodi e os logs de diagnóstico. **Não** use runs com `failure`.
2. Grave no cartão, ligue o cabo USB e faça os testes da seção **"Testes a fazer"**.
3. Confirme que o **SSH** entra: `ssh root@10.55.0.1` (senha `root`). Se entrar, dá
   para depurar ao vivo (dmesg, journalctl, `kodi.log`).
4. Me mande o **`diagnostico.txt`** e, se o Kodi não abrir, o **`kodi-start.log`**
   (e o `weston.log` quando o teste for de rotação/áudio/vídeo).
5. Com os dados eu fecho cada hipótese: a tabela **"Confirmado x hipótese"** diz o
   que ainda é suposição.

## Estado do CI

| Run | Commit | Resultado | Observação |
| --- | --- | --- | --- |
| #116 | `b73eec7` (tag v0.6) | **success** | **variante retroarch, 536 MB**: as 4 correções juntas |
| #115 | `b73eec7` (main) | success | kodi, valida o refactor das variantes |
| #112 | `a5e80bf` (tag v0.4) | failure | `rootfs/root` 0700: grep sem sudo; pacotes e 12 cores OK |
| #108 | `e1788c6` (tag v0.2) | failure | asserção do retroarch.cfg (barra final) |
| #106 | `04c32b1` (tag v0.1) | failure | 4 cores inexistentes para arm64 |
| #105 | `d42c856` | success | kodi 356 MB (após refactor) |
| #100 | `38c9e05` (BT + add-on) | **success** | **use esta**: Bluetooth corrigido (config UART gerado) + add-on Ouvila Radio com o teclado da senha certo; 356 MB |
| #99 | `5ec2ef9` (add-on) | success | add-on incluído, mas com o bug do teclado (só números) |
| #98 | `f0902f0` (BT config) | success | corrige o Bluetooth; ainda sem o add-on |
| #97 | `cbafb85` (docs stretch) | **success** | imagem testada: Wi-Fi ok no boot; BT ainda sem address |
| #96 | `7e82e19` (DTS wifi/bt) | **success** | fixes de SDIO (0004) e `dma-names` uart1 (0021) + retry; base do #97 |
| #94 | `0c9eb64` (Wi-Fi/BT + :8088) | **success** | artefato 356 MB: Wi-Fi/BT + painel web + rotate-90 |
| #93 | `2b9da4a` (Wi-Fi/BT) | **success** | painel web ainda na 8080 (conflito com Kodi) |
| #92 | `407fd46` (fix de fontes) | **success** | fixa U-Boot/ROCKNIX/rkbin + áudio; base do teste que validou `rotate-90` |
| #91 | `0a7ffb2` (áudio) | failure | ROCKNIX drift: patch X35S usa `&gpio_keys_control` (não existe no mainline 6.12) |
| #90 | `da0073f` (Kodi) | failure | mesmo drift (não era o Kodi: kernel não compilava) |
| #89 | `63f4bd7` (SSH) | **success** | imagem gravada e testada no X55; Kodi resolvido ao vivo |
| #88 | `84b38ea` (diagnóstico) | **success** | Kodi/USB diagnosticáveis + variante `x55-peripheral` |
| #87 | `e30e6`/`e430e61` (fix X55) | **success** | boot ativo da X55 + `rotate-90` |
| #84 | `836f3b5` (fix) | **success** | imagem antiga (bootava com DTB da X35S) |
| #83 | `664b057` (docs) | failure | bug de aspas do #82; ignorar |
| #82 | `68c5c77` (merge USB) | failure | bug de aspas no `build.yml` |
| #81 | `42e66b7` (merge rotação) | success | — |

> **Por que o #90/#91 falharam (drift de fontes externas):** o CI clonava a
> `ROCKNIX/distribution` no **master**. Em 23/10 esse master estava no kernel **7.2**,
> tinha **removido** o patch 0012 (joypad da X55) e reescrito o 0016 (X35S) usando o
> label `&gpio_keys_control`, que **não existe** no DTS da X55 do mainline 6.12 → o
> `make dtbs` falhava. Correção: U-Boot, ROCKNIX e rkbin **fixados por commit** (os
> commits que geraram a imagem validada) + guarda que falha na hora se um patch de DTS
> for rejeitado (deixava `.rej` e um DTS inconsistente).

O #82/#83 falharam sempre no step **"Build Minimal RootFS (Debian ARM64)"**, com
`ep0: command not found` e `here-document ... delimited by end-of-file`. Causa: o step
escreve arquivos com `sudo bash -c 'cat << "EOF" ...'`, e havia **aspas simples dentro**
desse bloco (fechavam o `'...'` do `bash -c`). Corrigido no #84 (aspas duplas e o
`dump-diagnostico.sh` deixou de ser copiado inline — agora vem de `variants/` via
`install`). A armadilha está na seção final deste documento.

Últimas mudanças grandes já mescladas no `main`: rotação configurável (PR #5) e remoção
do `dr_mode = "peripheral"` forçado nos DTBs da X35S (PR #6).

## Gerar e gravar a imagem

1. No GitHub: **Actions → "Build Mainline RK3566 (Powkiddy X55) OS"** → escolha o run
   **verde mais recente** → baixe o artifact. Para gerar de novo, use *Run workflow*.
   (Um push no `main` também dispara o build automaticamente.)
   Alternativa que não expira: **Releases** → `latest` (push na `main`) ou o release da
   tag `v*`/`retroarch-*`, com a imagem `.img.gz` e o `.sha256` ao lado.
2. Baixe o artifact **`rk3566-x55-mainline-sdcard-image`** (contém
   `rk3566-x55-mainline-sdcard.img.gz`).
3. Descompacte o `.gz`, grave o `.img` no cartão SD e insira no aparelho.

O nome do artifact continua "x55" só por compatibilidade: a mesma imagem atende os três
aparelhos, trocando o `FDT` no `extlinux.conf`.

## O que a imagem já tem (confirmado no código)

- **Boot:** `idbloader.img` no setor 64 e `u-boot.itb` no setor 16384; boot por
  `extlinux/extlinux.conf` na partição FAT `BOOT`; rootfs ext4 com rótulo `ROOTFS`.
- **FDT ativo:** `dtbs/rk3566-powkiddy-x55.dtb` (DTB da X55) — a imagem sai pronta para
  a X55, aparelho em teste. O DTB da X35S/X35H vai em `extlinux.conf.x35s`
  (`dtbs/rk3566-powkiddy-x35s-rocknix.dtb`, oficial da ROCKNIX com HDMI desabilitado, ou
  `dtbs/rk3566-powkiddy-x35s-nosso.dtb`, o nosso compilado). Se o DTB da X55 não for
  gerado, o step falha em vez de criar uma imagem com o FDT ausente.
- **`ouvila.conf`** na raiz da partição FAT, com `display_transform=rotate-90` (X55).
  Para X35S/X35H, troque para `normal`.
- **Entradas de boot alternativas** (presentes, mas não ativas) na pasta `extlinux/` da
  partição FAT:
  - `extlinux.conf` — a que está valendo (X55).
  - `extlinux.conf.x35s` — X35S/X35H (mesmo DTB).
  - `extlinux.conf.x35s-nosso` — nosso DTB compilado da X35S.
  - `extlinux.conf.x35s-peripheral` — DTB de teste com `dr_mode = "peripheral"` no USB.
  - `extlinux.conf.x55-peripheral` — o mesmo, mas para o DTB ativo da **X55** (use este
    para testar a ancoragem USB na X55).
  - Para usar qualquer uma: no PC, copie o conteúdo escolhido **por cima** de
    `extlinux.conf`.
- **Logs de diagnóstico no cartão** (partição FAT, leitura no PC):
  - `diagnostico.txt` — duas capturas (15s e 60s) com dmesg, entrada, USB, Kodi/Weston.
  - `kodi-start.log` — **saída do Kodi** (o motivo de a tela ficar em loop).
  - `usb-gadget.log` — o que o serviço de ancoragem USB tentou e o que achou de UDC.
  - `weston.log`/`weston-falhou.log` — compositor.
- **Diagnóstico:** `diagnostico.txt` na raiz da partição FAT, gravado pelo serviço
  `dump-diagnostico` cerca de 15 s depois do boot. Tire o cartão e leia no PC. O script
  vem de `variants/kodi/overlay/usr/local/bin/dump-diagnostico.sh` (o CI o instala com
  `install`; não há mais cópia inline no `build.yml`).
- **Acesso:** a senha do `root` é `root` e o SSH está habilitado. A imagem instala
  `/etc/ssh/sshd_config.d/10-ouvila.conf` com `PermitRootLogin yes` +
  `PasswordAuthentication yes` (o Debian 12 bloqueia login do root por senha por
  padrão — sem esse drop-in, `root:root` é recusado). Pelo cabo USB o aparelho fica
  em **10.55.0.1**. Ver "Segurança" no `docs/roadmap.md`.
- **Painel web de Wi-Fi/Bluetooth:** `ouvila-webui.service` (busybox httpd) na porta
  **8088**, servindo `variants/kodi/overlay/usr/local/share/ouvila-webui/`. Acesse
  `http://10.55.0.1:8088` pelo cabo (ou pelo IP do Wi-Fi) para procurar/conectar redes
  e parear dispositivos Bluetooth — o Kodi em si não gerencia rádio.
  Wi-Fi usa `nmcli`/`wpa_supplicant`; BT usa `bluetoothctl`/`bluez`.
- **Add-on dentro do Kodi (`script.ouvila.radio`, "Ouvila Radio"):** o Kodi 20 no
  Linux **não tem** gerenciador de rádio (o de Wi-Fi existe só nos builds
  Windows/Android), então o projeto traz um add-on Python em
  `variants/kodi/overlay/usr/share/kodi/addons/script.ouvila.radio/`. Ele fala direto
  com o `nmcli` (Wi-Fi: redes, conectar, desconectar) e o `bluetoothctl` (BT: ligar,
  procurar, parear, conectar, ficar visível). Aparece em **Add-ons → Add-ons de
  programas**. Validado no aparelho: o `CAddonMgr` registra
  `script.ouvila.radio v1.0.0 installed` e as leituras reais (redes Wi-Fi, controlador
  BT, scan) funcionam. A **GUI** também foi validada: via JSON-RPC do Kodi (TCP **9090**)
  o add-on abriu o diálogo, a opção *Wi-Fi: procurar redes* rodou o scan e listou redes
  reais, sem traceback. Dá para repetir por SSH: `Addons.ExecuteAddon` +
  `Input.ExecuteAction` (`down`/`select`/`back`) + `GUI.GetProperties`.
- **Firmware de rádio:** o CI baixa do `linux-firmware` (tag **20260309**, sha256
  conferido) `rtw88/rtw8821c_fw.bin` e `rtl_bt/rtl8821cs_fw.bin`, e **gera** o
  `rtl_bt/rtl8821cs_config.bin` (25 bytes, com a entrada UART `0x0c`) — ver
  `docs/findings.md`.

## Confirmado x hipótese

| Item | Estado |
| --- | --- |
| Boot, painel, U-Boot, Debian/Kodi | **confirmado** (testes anteriores) |
| HDMI desabilitado resolve a tela preta | **confirmado** |
| `rocknix-joypad` no X55 (17 botões / 4 eixos) + mapa Kodi | **confirmado** |
| Weston com `drm-backend.so` + `kiosk-shell` | **confirmado** |
| Kodi do Debian bookworm é 20.1 **com Wayland** (`BUILD_WAYLAND=yes`) | **confirmado (código)** |
| X55: Kodi inicia e mostra a interface | **RESOLVIDO** (Weston criava `wayland-1`; corrigido com `--socket=wayland-0`) |
| X55: rotação da interface | **confirmado no aparelho: `rotate-90`** (landscape 1280x720) |
| USB gadget (ancoragem) no X55: rede `usb0` no PC | **confirmado** (10.55.0.1 responde e a porta 22 abre, verificado do PC em 2026-10-09) |
| USB na X35H | **não ancora** e não é o papel OTG (testado 2026-10-24). Hipótese: porta é host-only (`usb@fd000000`). Teste: pendrive + `ls /sys/bus/usb/devices/` |
| Login do root por senha no SSH | **era bloqueado** (`prohibit-password`); corrigido no #89 e **validado** |
| `dr_mode = "peripheral"` faz a ancoragem USB funcionar | **DESCARTADO na X35S/X35H** (testado: PC não vê nada, UDC `not attached`); na X55 funciona com `otg` |
| Sentido da rotação no X55 | **confirmado no aparelho: `rotate-90`** |
| Contagem de botões/eixos na X35H | **medido: botões e direcionais OK** (2026-10-24) |
| X35H: vídeo, áudio e botões | **CONFIRMADO no aparelho (2026-10-24)**: 640x480 a 58,5 Hz em tela cheia, áudio e direcionais OK |
| Áudio (I2S habilitado no #92) | **CONFIRMADO no aparelho (2026-10-24): som no jack 3,5 mm e no alto-falante interno**, com `rk817_ext`, `Playback Mux=HP` e detecção de jack por GPIO |
| Wi-Fi RTL8821CS | **confirmado no #97**: `wlan0` sobe no boot sem retry (fix SDIO 0004 + `dma-names` uart1 no DTS) |
| Bluetooth RTL8821CS | **confirmado em 2026-10-24**: faltava o `rtl8821cs_config.bin` com a entrada UART `0x0c`; o CI agora gera o arquivo → `hci0` com MAC real, `UP`, scan ok, e sobe no boot |
| Painel web de Wi-Fi/BT na porta 8088 | **implementado** e testado localmente (busybox httpd) |
| Vídeo por hardware (`va_openDriver() returns -1`) | **aberto** |

## Testes a fazer (na ordem)

### 0. Kodi não inicia no X55 (tela em loop) — ✅ RESOLVIDO em 2026-10-23

Causa raiz confirmada no aparelho: o **Weston 10.0.1 criava o socket como
`wayland-1`** (porque `WAYLAND_DISPLAY` estava no ambiente); o launcher esperava
`wayland-0`, dava timeout e matava um Weston que estava funcionando. Correção:
`--socket=wayland-0` explícito (+ 3 s antes do fallback GBM). Validado com **reboot**:
`kodi.service` ativo, Weston + Kodi em Wayland, `GUI format 720x1280`,
`xdg_output` lógico **1280x720** (landscape).

**Falta só a confirmação visual:** se a imagem estiver de cabeça para baixo, troque
`display_transform` em `ouvila.conf` para `rotate-270` (já está `rotate-90`, validado).

### 0.1 Wi-Fi e Bluetooth (imagem nova, 2026-10-24)

1. Acesse **`http://10.55.0.1:8088`** pelo cabo USB (ou pelo IP do Wi-Fi, se já
   conectado). O painel tem duas seções: **Wi-Fi** e **Bluetooth**.
2. Wi-Fi: **Procurar redes** → escolher → senha → **Conectar**. Confirme com
   `ip addr` (SSH) ou no próprio painel (mostra estado/IP).
3. Bluetooth: **Ligar** → **Procurar dispositivos** → **Parear**. Ele pareia, confia
   e conecta. Se o dispositivo pedir confirmação, aceite/repetir o pareamento.
4. Se algo falhar, me mande: `dmesg | grep -iE 'rtw88|hci0|btrtl'`,
   `nmcli dev wifi list`, `bluetoothctl show` e `journalctl -u ouvila-webui`.

### 1. USB gadget na X35H (prioridade)

Objetivo: ver se, sem forçar `peripheral`, o PC enxerga a `usb0`.

1. Boot normal (sem mexer em nada) no X35H, com o cabo USB-C ligado ao PC.
2. Tire o cartão, abra o `diagnostico.txt` no PC e procure, na seção de USB:
   - `dr_mode` do nó `usb@fcc00000` — o esperado agora é **`otg`**;
   - as linhas de `dmesg` filtrado — se aparecer `failed to enable ep0out`, ainda falha.
3. **Se funcionar** (o PC lista a rede `usb0`): me diga e o item está fechado.
4. **Se falhar:** no PC, na partição FAT, copie
   `extlinux/extlinux.conf.x35s-peripheral` por cima de `extlinux/extlinux.conf`
   (isso usa o DTB com `dr_mode = "peripheral"`), boote de novo e repita o passo 2.

O mesmo teste na **X55** usa `extlinux/extlinux.conf.x55-peripheral` (o DTB ativo da
X55 com USB forçado a `peripheral`). O `usb-gadget.log` no cartão mostra o que o
serviço fez e se apareceu algum UDC.

Hipótese em teste: a única diferença de USB entre a imagem que funciona (X55) e a que
falhava era o `peripheral` que forçávamos. Se as duas opções falharem, a causa passa a
ser hardware (VBUS/ID da porta, cabo sem fios de dados, ou a porta ser host-only).

**Limitação conhecida deste diagnóstico:** o `dr_mode` que o CI imprime no log do
Actions usa `grep -A5` e acaba **não** mostrando o atributo (ele fica depois das 5
linhas). Ou seja, o log do CI **não** serve para conferir esse valor — use a seção USB
do `diagnostico.txt`, que traz o `dr_mode` do device tree em uso.

### 2. Rotação da tela no X55

1. Não precisa copiar `extlinux.conf`: a imagem já sai com o DTB da X55 **e** a rotação
   da X55 (`display_transform=rotate-90`) ativos.
2. Se a interface ficar de cabeça para baixo, edite `ouvila.conf` na partição FAT e
   troque para `display_transform=rotate-90`.
3. Me diga **qual valor deixa a interface correta** e, se possível, mande o trecho do
   `weston.log` (ele está no `diagnostico.txt`) — nele aparece se houve
   `Invalid transform`.

Valores válidos (Weston): `normal`, `rotate-90`, `rotate-180`, `rotate-270`, `flipped`,
`flipped-rotate-90`, `flipped-rotate-180`, `flipped-rotate-270`. Um valor inválido cai
em `normal`, com aviso no journal.

### 3. Gamepad na X35H

No `diagnostico.txt`, procure `/proc/bus/input/devices` e veja se aparece o
`retrogame_joypad` e com quantos botões/eixos. O mapa atual do Kodi vale para
**17 botões e 4 eixos**; se a X35H tiver outra contagem, precisamos de um mapa novo.

### 4. Áudio e vídeo

- Áudio: mande o trecho do `dmesg` com `asoc-simple-card` / `parse error`.
- Vídeo: mande a linha com `va_openDriver`.

## O que me enviar

- O **`diagnostico.txt` inteiro** (é o mais útil: tem `dmesg`, entrada, UDC/role, USB,
  Kodi e Weston).
- Se o teste for de rotação, o trecho do **`weston.log`**.
- Se for de áudio ou vídeo, o trecho do `dmesg` correspondente.

O log do CI e o `diagnostico.txt` são coisas **diferentes**: o log do CI mostra a
compilação; o `diagnostico.txt` mostra o que aconteceu no aparelho.

## Pendências de código (para eu resolver, não dependem do aparelho)

1. O dump do nó USB no log do CI usa `grep -A5` e não imprime o `dr_mode` (fica fora das
   5 linhas). Trocar por algo que mostre o atributo (ex.: `grep -A20` ou filtrar só o
   `dr_mode`). **Não refiz o build só por isso**, para não trocar o artefato que você vai
   baixar; se mexer nisso, gere um run novo e use o artefato do run mais recente.
2. O `build.yml` ainda monta a imagem **inline** (não usa o `build/create-image.sh`).
   Duplicação pendente; a rotação já não depende disso (o CI grava `rotate-90` para a
   X55, e o `create-image.sh` lê `display_transform` de `device.conf`).
3. ~~Mover o padrão de rotação do `case` do `create-image.sh` para `display_transform=`
   em `devices/<placa>/device.conf`.~~ **Feito:** `devices/*/device.conf` tem
   `display_transform`, o `build.sh` repassa como 9º argumento e o `create-image.sh` usa
   esse valor (o `case` fica só como fallback).

## Onde ficam as coisas no repositório

- `docs/findings.md` — o que foi aprendido, com marcação confirmado/hipótese.
- `docs/roadmap.md` — prioridades, incluindo o que depende de teste no aparelho.
- `docs/build.md` — fluxo de build e detalhes de painel, gamepad, rotação e USB.
- `docs/porting.md` — como adicionar outra placa.
- `.github/workflows/build.yml` — o pipeline completo (é ele que monta a imagem).
- `build/create-image.sh` — empacotador local (o CI **não** usa este ainda).
- `variants/kodi/overlay/` — serviços e scripts que vão para o rootfs (Weston, Kodi,
  `dump-diagnostico`, USB gadget).
- `tools/joypad_from_dtb.py` — extrai o nó `rocknix-joypad` de um `.dtb` e compara com
  o mapa do X55.

## Armadilhas já vividas (para não repetir)

- Terminador de heredoc dentro de um `run: |` precisa ficar na indentação base do bloco.
- `: ` (dois-pontos e espaço) no `name:` de um step quebra o YAML.
- `find` retorna 0 mesmo quando não acha nada — não dá para checar por código de saída.
- Em `sudo bash -c '...'` no `build.yml`, **não usar aspas simples** no conteúdo: elas
  fecham o `'...'` do `bash -c` e quebram o heredoc. Foi o que derrubou o CI no run #82,
  com `ep0: command not found` e
  `here-document ... delimited by end-of-file (wanted 'EOF')`. Usar aspas duplas.
- **`bash -n` não pega esse erro** de aspas: o número de aspas pode ficar par e o
  arquivo "parseia", mas o heredoc sai errado. Validar a estrutura à parte.
- Não duplicar scripts entre o `build.yml` e `variants/`: o `dump-diagnostico.sh` era
  mantido nos dois lugares e as cópias divergiram. Agora o CI instala o arquivo do
  overlay com `install` (fonte única em `variants/kodi/overlay/usr/local/bin/`).
- Alterar um `.dts` **depois** de `make dtbs` não afeta o `.dtb` já compilado.
- O log do CI e o `diagnostico.txt` são arquivos diferentes (ver acima).
- Não inventar GPIOs, nomes de `CONFIG_` nem valores de pinagem: usar só fonte confirmada.

> Para levar estas placas a outro sistema, use
> **[Hardware e drivers por dispositivo](hardware-e-drivers.md)** (arquitetura + receitas
> F1–F12 de correção).

## Variantes

| Variante | Artefato | Estado |
| --- | --- | --- |
| `kodi` (multimídia) | `rk3566-x55-mainline-sdcard-image` | validada no X55 e X35H |
| `retroarch` (jogos) | `rk3566-x55-retroarch-sdcard-image` | **build #116 success (536 MB); falta teste em hardware** |
| `media-player` (app único) | `rk3566-x55-media-player-sdcard-image` | **X35H: sobe, desenha e toca** (`media-player-v0.1`); correções de controle e o fim-de-faixa na v0.2 |

Escolha no `Run workflow` (input `variant`); push na `main` constrói `kodi`. Detalhes em
[variantes.md](variantes.md).
