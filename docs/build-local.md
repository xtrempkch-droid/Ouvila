# Build local da imagem (sem GitHub Actions)

O CI é o caminho oficial, mas gerar a imagem **nesta máquina** não pode ficar refém de
token (`gh` não existe aqui e o artefato do Actions exige login). O driver é
[`tools/build-local-rk3326.sh`](../tools/build-local-rk3326.sh) e ele reproduz os
**mesmos passos** de [`.github/workflows/build-rk3326.yml`](../.github/workflows/build-rk3326.yml)
com os mesmos padrões (`loader=oficial`, `kernelSource=proprio`, `v7.1`). Se um dia
divergirem, **o workflow é a referência**.

## Uso

```sh
tools/build-local-rk3326.sh                     # media-player (padrão)
tools/build-local-rk3326.sh --variante kodi
tools/build-local-rk3326.sh --variante retroarch
tools/build-local-rk3326.sh --saida /caminho    # onde deixar o .img.gz
tools/build-local-rk3326.sh --reaproveitar      # mantém o container após o sucesso
```

Saída em `out-rk3326/` (ignorado pelo git):

| Arquivo | O que é |
| --- | --- |
| `rk3326-r36s-<variante>-local-sdcard.img.gz` + `.sha256` | a imagem para gravar |
| `host.log` | log do lado do host (o que o container imprimiu) |
| `build-local.log` | o mesmo, gravado de dentro do container |
| `cache/rocknix.img.gz` | 1,2 GB da ROCKNIX, reaproveitado entre tentativas |

```sh
gunzip -c out-rk3326/*.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

Console do aparelho: **ttyS2, 1500000 8N1** (TTL).

## Como ele funciona

O host só precisa de **`docker`**. Tudo acontece num container `ubuntu:24.04` rodando
com `--privileged -u 0` — sem root não há loop device nem `mount`, e sem `mount` não há
imagem. O repositório entra **somente leitura** em `/src` e é copiado para `/work` lá
dentro; só o diretório de saída vem do host. Isso mantém a árvore de trabalho livre de
`kernel/`, `rootfs/`, `uboot/` e `img/`.

Os 8 passos: dependências → 16 MiB do loader oficial (sha256 fixo) → kernel + drivers
da ROCKNIX → DTB + overlay de painel → rootfs Debian arm64 → app de mídia (cross) →
serviço do tty1 → imagem.

**O container fica de pé quando o build falha.** Dentro de `/work` estão o kernel
compilado (~35 min) e o rootfs; a próxima chamada recopia só o repositório e continua de
onde parou. Para começar do zero: `docker rm -f ouvila-rk3326-build`.

## Armadilhas que custaram tempo

1. **`docker run --privileged` sozinho NÃO dá root.** Sem `-u 0` o container roda como
   uid 1000, `CapEff` é `0000000000000000` e `losetup`/`mount` falham. Precisa dos dois.
2. **`/tmp` do host não é compartilhado com os containers** (só `$HOME`, no Docker
   Desktop). Qualquer `-v /tmp/...` aponta para o `/tmp` do container.
3. **`docker run --rm` joga fora o que foi instalado.** Para trabalho iterativo usei
   container nomeado + `docker exec`.

### A armadilha que derrubou a máquina inteira (2026-11-07)

O `chroot` arm64 do `debootstrap` precisa que o kernel saiba executar ELF aarch64 via
`binfmt_misc`. Registrar o handler assim **quebra o host**:

```sh
printf ':qemu-aarch64:M::\x7fELF\x02\x01\x01\x00...' > /proc/sys/fs/binfmt_misc/register
```

A magic correta tem 20 bytes e **contém NULs** (`7f 45 4c 46 02 01 01 00 …`). O `printf`
do bash os produz, mas a escrita em `/proc/sys/fs/binfmt_misc/register` é uma **string C**:
o primeiro `\x00` a **trunca**. O registro entrou com **7 bytes** — exatamente
`7f454c46020101`, tudo o que vem antes do primeiro NUL. Os outros 13 bytes (o restante
de `e_ident` + `e_machine = 0xb7`) nunca foram gravados.

Uma `magic` de 7 bytes casa com **qualquer ELF de 64 bits**, inclusive os do host: a
partir dali `bash`, `ls` e `docker` passam a ser entregues ao `qemu-aarch64-static`, que
não reconhece x86-64 e devolve `ENOEXEC` — a máquina inteira para de executar, com
`Exec format error`. Desfazer exige root, que nesta máquina não existe (`sudo`
indisponível). O estado ficou visível em `/proc/sys/fs/binfmt_misc/qemu-aarch64`:

```text
enabled
interpreter /usr/bin/qemu-aarch64-static
flags: OCF
offset 0
magic 7f454c46020101          <- 7 bytes, deveria ser 20
mask ffffffffffffff           <- idem
```

O que o script faz hoje:

- registra em **Python** (`bytes.fromhex`), com os 20 bytes exatos
  (`7f454c460201010000000000000000000200b700` /
  `ffffffffffffff00fffffffffffffffeffffff`);
- **confere na volta** lendo `/proc/sys/fs/binfmt_misc/qemu-aarch64`: se `magic`/`mask`
  não baterem, **desfaz na mesma hora** (`echo -1`) e aborta;
- **remove o handler no fim** (trap `EXIT`), para o ambiente voltar como estava.

Para se recuperar de um registro ruim, num terminal com root:

```sh
sudo sh -c 'echo -1 > /proc/sys/fs/binfmt_misc/qemu-aarch64'
# ou, se o arquivo nem abre:  reiniciar a máquina (registros não são persistentes)
```

Se nem `bash` executa, o `echo` acima tem de ser feito por um **builtin** do shell que
já está rodando (o `sh` redireciona para o procfs sem precisar de `fork`/`exec`).

## O que ainda **não** está testado

O build local nunca chegou ao fim nesta máquina (parou no `debootstrap`, pelo problema
acima). Ou seja: loader, DTB/overlay, kernel e rootfs já foram gerados e conferidos, mas
**a imagem do cartão para o R35S ainda não foi produzida nem bootada**. Ver
[`docs/status.md`](status.md) para o ponto de retomada.
