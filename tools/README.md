# Tools

Utilitários auxiliares independentes do fluxo principal entram aqui. O empacotador de imagem fica em `build/` porque é parte direta do pipeline de build.

| Ferramenta | Para quê |
| --- | --- |
| [`ttl-extract.py`](ttl-extract.py) | **Extrai o hardware de um aparelho pelo console serial (TTL)**: identificação, painel, entrada, áudio, energia, armazenamento, USB e o **device tree vivo** (`/sys/firmware/fdt`). Só lê — não instala nem escreve nada no aparelho |
| [`fdt-dump.py`](fdt-dump.py) | Lê um `.dtb` sem precisar de `dtc`: modos `dts`, `no`, `prop`, `lista` e `phandles` (para resolver `<&gpio3 16 1>`). O CI tem `dtc`; a máquina de trabalho do projeto não |
| [`joypad_from_dtb.py`](joypad_from_dtb.py) | Tira o mapa do `rocknix-joypad` do DTB (Kodi/RetroArch/media-player) |
| [`panel_overlay_from_dts.py`](panel_overlay_from_dts.py) | Converte o bloco `panel@0` de um DTS de fabricante no overlay `rocknix,generic-dsi` |
| [`build-local-rk3326.sh`](build-local-rk3326.sh) | **Gera a imagem do R35S/R36S nesta máquina**, sem GitHub Actions (docker + `--privileged -u 0`). Ver [`docs/build-local.md`](../docs/build-local.md) |
| [`install-image-to-emmc.sh`](install-image-to-emmc.sh) | Grava a imagem no eMMC da X35H (confere tamanho e zera o GPT) |
| [`test-media-player.sh`](test-media-player.sh) | Testes offline da variante `media-player` (biblioteca, UI, idiomas, temas, decoder, launcher) |

## Extrair o hardware de um aparelho (R35S/R36S, X55, X35H)

O console serial é o único caminho para ler o hardware de um aparelho que não sobe
imagem nossa (ou que ainda está com o sistema de fábrica). O script pressupõe **shell de
root** no console — as imagens do projeto já têm autologin; num sistema de fábrica é
preciso que ele abra shell.

```sh
./tools/ttl-extract.py --porta /dev/ttyUSB0 --baud 1500000 --placa r35s --destino saida/
```

Saem, em `saida/`: `relatorio.txt` (respostas cruas), `hardware.md` (resumo legível),
`<placa>-live.dtb` (device tree da unidade) e `<placa>-live.dts` (o mesmo decodificado).

Quatro detalhes que custaram tempo e já estão resolvidos no script:

- **baud**: 1 500 000 no R35S/R36S e no X55 (`ttyS2`); se sair lixo, é isso;
- **eco do terminal**: o console devolve tudo o que se digita e quebra a linha em 80
  colunas, então o eco se mistura com a resposta. O script faz `stty -echo` enquanto
  conversa e restaura o eco ao terminar;
- **o FDT é grande** (57 KB no R35S): vai comprimido em base64 (`gzip -9c | base64`),
  senão a transferência demora e perde bytes;
- **sem permissão na porta serial**: rode dentro de um container com
  `--device=/dev/ttyUSB0` e destino em `$HOME` (o Docker Desktop compartilha `$HOME`,
  não `/tmp`).

Exemplo real (R35S, 2026-11-06): o resultado está em
[`docs/r35s-rk3326.md`](../docs/r35s-rk3326.md) §12 e o DTB em
[`devices/game-console-r35s/dts/referencia/`](../devices/game-console-r35s/dts/referencia/Readme.md).

## Gerar a imagem localmente (R35S/R36S)

```sh
./tools/build-local-rk3326.sh --variante media-player
```

Precisa de `docker`, ~25 GB livres e internet. A imagem sai em `out-rk3326/`, com
`.sha256`. O script reaproveita o container entre tentativas (o kernel compilado fica em
`/work`), então uma falha no meio do caminho não custa outros 35 min de kernel.
Detalhes, tempo esperado e as armadilhas (inclusive a do `binfmt_misc` que derrubou a
máquina) estão em [`docs/build-local.md`](../docs/build-local.md).
