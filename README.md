# Ouvila

Sistema Linux para media players RK3566, com foco inicial nos handhelds Powkiddy X55, X35S e X35H.

> **Status (2026-10-25):** três variantes compilando; duas testadas em hardware.
> - **`kodi` (multimídia)** — build **#120**, validada no **X55** (vídeo, áudio,
>   botões, Wi-Fi, Bluetooth, USB) e no **X35H** (vídeo, áudio, botões). No X35H **não
>   há Wi-Fi nem Bluetooth** e a porta USB não ancora — ver [docs/status.md](docs/status.md).
> - **`retroarch` (jogos)** — build **#119**, imagem pronta (536 MB) e **validada no
>   X55 e no X35H** (tela girada, áudio, todos os botões e os dois analógicos). Falta
>   testar um jogo de verdade. Checklist em [docs/status.md](docs/status.md).

> **Status técnico:** em desenvolvimento. O workflow em `.github/workflows/build.yml`
> mantém o build existente com U-Boot, Linux v6.12, ROCKNIX, Debian ARM64 e Kodi/GBM.
> X55 tem device tree mainline; X35S continua em investigação e X35H não possui
> configuração de build independente validada (usa o DTB do X35S, como a ROCKNIX).

## Estrutura

```text
common/
  kernel/                 fragmentos e política compartilhada do kernel
  packages/               pacotes comuns do rootfs
  overlay/                arquivos compartilhados instalados no rootfs
devices/<device>/
  dts/                    fontes e referências de device tree
  uboot/                  configuração e referências de U-Boot
  kernel.fragment         fragmento específico, quando validado
  device.conf             metadados de build da placa
variants/<variant>/
  packages/               pacotes da variante
  overlay/                serviços e arquivos específicos da variante
  src/                    código do app (variante media-player)
  variant.conf            metadados da variante
build/
  build.sh                entrada para empacotar <device> <variant>
  create-image.sh         criação da imagem SD
tools/                    utilitários auxiliares
docs/                     build, arquitetura e porting
.github/                  workflow, templates e CODEOWNERS
```

## Dispositivos e variantes

| Identificador | Modelo | Estado |
|---|---|---|
| `powkiddy-x55` | Powkiddy X55 | Device tree mainline conhecido |
| `powkiddy-x35s` | Powkiddy X35S | DTS/DTB de referência ROCKNIX; em investigação |
| `powkiddy-x35h` | Powkiddy X35H | Sem DTS/DTB independente validado |
| `game-console-r35s` | Game Console R35S / R36S | **RK3326** (não RK3566): preparada, fora deste pipeline — ver [docs/r35s-rk3326.md](docs/r35s-rk3326.md) |

O projeto tem **três variantes** (o mesmo kernel/DTB, mudando o que entra no rootfs):
**`kodi`** (multimídia), **`retroarch`** (jogos) e **`media-player`** (app de mídia
único: um binário em C que desenha no DRM/KMS, decodifica com o FFmpeg e toca no ALSA,
sem compositor, navegador nem player externo). Detalhes, como escolher no build e as
armadilhas de driver em [docs/variantes.md](docs/variantes.md).

A variante padrão do build é `kodi` (Debian Bookworm + Weston/Wayland). Uma única imagem atende os três aparelhos: o device tree e a rotação mudam por arquivo no cartão (`extlinux/extlinux.conf.*` e `ouvila.conf`), sem recompilar. O teste no Powkiddy X55 revelou tela preta com erros no caminho DSI/VOP; o workflow desabilita HDMI como workaround experimental, conforme `docs/build.md`. Valores de pinagem, GPIO e display não são inferidos; consulte `docs/porting.md` antes de adicionar hardware.

`variants/own/` permanece como scaffold planejado: não contém pacotes nem overlay instalável e o empacotador recusa essa variante até que seja implementada.

## Empacotar uma imagem

Depois de preparar os artefatos de U-Boot, kernel e rootfs, execute:

```sh
build/build.sh powkiddy-x55 kodi
```

Por padrão, o script procura `uboot/`, `kernel/` e `rootfs/` na raiz do repositório e grava a imagem compactada em `output/`. Os caminhos podem ser definidos por `OUVILA_UBOOT_DIR`, `OUVILA_KERNEL_DIR`, `OUVILA_ROOTFS_DIR` e `OUVILA_OUTPUT_DIR`; caminhos relativos também são resolvidos a partir da raiz do repositório. `OUVILA_DTB_SOURCE` pode apontar para um DTB de referência já obtido; sem isso, usa o DTB compilado no diretório do kernel. A rotação da interface vem de `display_transform` no `device.conf` (X55 `rotate-90`, X35S/X35H `normal`) e é gravada em `ouvila.conf` na partição de boot. Wi-Fi/Bluetooth são gerenciados por um **add-on dentro do Kodi** (`Ouvila Radio`, em Add-ons → Add-ons de programas) e pelo painel web em `http://10.55.0.1:8088` — o Kodi 20 no Linux não traz UI de rádio. O script não baixa fontes nem monta o rootfs: no momento, o workflow do GitHub Actions é o fluxo completo de build.

Para executar o build completo, use **Actions → Build Mainline RK3566 (Powkiddy X55) OS → Run workflow**. O workflow publica a imagem em **Releases**: tags `v*`/`retroarch-*` geram um release da versão e cada push na `main` atualiza o release rolante **`latest`** (pre-release). Baixe o `.img.gz` e o `.sha256` de lá — o artifact do Actions continua existindo, mas expira. Detalhes e como gravar em [docs/build.md](docs/build.md#publicação-no-github-releases).

Uma única imagem atende os três aparelhos (X55, X35S e X35H — X35S e X35H compartilham o mesmo DTB): o boot ativo sai com o **DTB da X55**; para X35S/X35H, copie `extlinux/extlinux.conf.x35s` por cima de `extlinux.conf` e ajuste `display_transform=normal` em `ouvila.conf`. O artifact mantém o nome histórico `rk3566-x55-mainline-sdcard-image` por compatibilidade, e a imagem não representa validação em hardware.

## Créditos e transparência sobre o uso de IA

**Os códigos deste repositório foram escritos por inteligência artificial.** A escolha
é declarada de propósito: quem visita o projeto deve saber como ele foi produzido, o
que foi verificado em hardware e o que ainda é hipótese.

- **Modelo principal:** **DeepSeek** — responsável pela criação dos códigos do projeto:
  o workflow de build (`.github/workflows/build.yml`), os scripts de build e imagem
  (`build/`), os overlays de rootfs e serviços systemd (`variants/kodi/overlay/`), o
  painel web (`ouvila-webui`), o add-on `script.ouvila.radio`, os scripts de
  diagnóstico e a documentação em `docs/` e `AGENTS.md`.
- **Assistente de codificação:** GitHub Copilot (agente de código no VS Code), usado
  para operar o mesmo trabalho — ler logs do aparelho, propor correções e editar os
  arquivos. Os commits trazem o trailer `Co-authored-by: Copilot`.

### O que isso significa na prática

- **Verificado em hardware:** tudo o que está marcado como *confirmado* em
  [docs/findings.md](docs/findings.md) e na tabela **"Confirmado x hipótese"** de
  [docs/status.md](docs/status.md) foi testado num **Powkiddy X55 real** (boot, tela,
  rotação `rotate-90`, controles, áudio, Wi-Fi, Bluetooth, USB e SSH).
- **Não verificado:** **X35S e X35H nunca foram testados em hardware** por quem
  escreveu estes códigos. O que existe para eles é o DTB de referência da ROCKNIX e
  configuração de build — trate como experimental.
- **Código gerado por IA erra:** os erros que já apareceram (DTB errado no boot, socket
  `wayland-1` do Weston, `PermitRootLogin`, config de Bluetooth ausente, patches de DTS
  silenciosamente ignorados) estão documentados em [AGENTS.md](AGENTS.md) e em
  [docs/findings.md](docs/findings.md) justamente para não voltarem. Ao revisar,
  desconfie: nada aqui é dogma.
- **Licenças de terceiros:** o projeto reutiliza código e referências de **ROCKNIX**,
  **Linux mainline**, **U-Boot**, **rkbin**, **linux-firmware**, **Debian** e **Kodi**,
  cada um sob a sua própria licença. Onde algo foi copiado ou adaptado, o caminho é
  citado no arquivo (por exemplo, os overrides de DTS que reproduzem patches da
  ROCKNIX). Os créditos desses projetos são deles, não deste repositório.

> Se você for reutilizar este trabalho, mantenha esta seção. O objetivo não é
> "parecer feito à mão", é deixar claro o que é criação assistida por IA, o que foi
> validado num aparelho de verdade e o que ainda é tentativa.


## Documentação

- [Estado do projeto e retomada](docs/status.md) — comece por aqui
- [Game Console R35S (RK3326): painel e port](docs/r35s-rk3326.md) — placa fora do pipeline RK3566
- [Fluxo de build](docs/build.md)
- [Variantes: `kodi` (multimídia), `retroarch` (jogos) e `media-player`](docs/variantes.md)
- [Hardware e drivers por dispositivo (referência de portabilidade)](docs/hardware-e-drivers.md) — o que copiar para levar o aparelho a outro sistema
- [Descobertas de hardware (confirmado/hipótese)](docs/findings.md)
- [Roadmap](docs/roadmap.md)
- [Porting de dispositivos](docs/porting.md)
- [Arquitetura e estado da migração](docs/architecture.md)
