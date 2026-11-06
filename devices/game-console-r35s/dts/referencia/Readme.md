# Device tree lido do aparelho (referência)

`rk3326-gameconsole-r35s-live-6.12.dtb` é o **device tree vivo** desta unidade de
R35S: o blob que o kernel recebeu do bootloader, copiado de `/sys/firmware/fdt`
pelo console serial (TTL) em **2026-11-06**.

- sha256 `27449a06…` · 57 344 bytes
- origem: aparelho ligado, `cat /sys/firmware/fdt` (comprimido em base64 na serial)
- modelo no DT: `Game Console R36S`
- `compatible`: `gameconsole,r35s`, `gameconsole,r36s`, `rockchip,rk3326`

## Para que serve

O DTB que o **CI embarca** continua sendo o oficial da ROCKNIX
([`../rk3326-gameconsole-r36s.dtb`](../rk3326-gameconsole-r36s.dtb), sha256
`b84245e6…`), porque ele foi compilado com `-@` (tem `__symbols__`, necessário
para aplicar o overlay do painel) e é o que a documentação já validou. Este
arquivo aqui é a **referência do hardware da unidade**:

- confirma que o painel do aparelho é exatamente o que o nosso overlay descreve
  (160 entradas iguais, ver `docs/r35s-rk3326.md` §12);
- guarda os GPIOs, o ADC e os reguladores medidos, para comparar se aparecer um
  clone com painel ou botões diferentes;
- serve para tirar dúvida quando o aparelho não liga com o DTB do CI: se o DT
  oficial e este divergirem num nó, a diferença está no commit.

## Como ler

```sh
./tools/fdt-dump.py devices/game-console-r35s/dts/referencia/rk3326-gameconsole-r35s-live-6.12.dtb dts
./tools/fdt-dump.py .../rk3326-gameconsole-r35s-live-6.12.dtb no /odroidgo3-joypad
./tools/fdt-dump.py .../rk3326-gameconsole-r35s-live-6.12.dtb phandles
```

Não precisa de `dtc`: o `tools/fdt-dump.py` lê o blob direto (o formato está
comentado no cabeçalho do script).

## Como reextrair (outra unidade, ou depois de trocar o painel)

```sh
./tools/ttl-extract.py --porta /dev/ttyUSB0 --baud 1500000 --placa r35s --destino saida/
```

O aparelho precisa estar ligado com console serial e **shell de root** (o
autologin do console, que as imagens do projeto já têm). O script só lê: não
instala, não escreve e não reinicia nada. Ele desliga o eco do terminal
(`stty -echo`) enquanto conversa, porque o eco quebra as linhas de 80 colunas e
mistura o comando com a resposta — e restaura o eco ao terminar.

Sem permissão na porta serial, o caminho que funcionou aqui foi rodar dentro de
um container com `--device=/dev/ttyUSB0` e um diretório de saída em `$HOME`
(o Docker Desktop compartilha `$HOME`, não `/tmp`).
