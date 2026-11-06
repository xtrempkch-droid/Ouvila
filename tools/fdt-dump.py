#!/usr/bin/env python3
"""Leitor de FDT (flattened device tree) em Python puro — não precisa de dtc.

Por que existe: as máquinas onde este repositório é trabalhado (e os runners do
CI) não têm `device-tree-compiler`. Para comparar o DTB que embarcamos com o que
o aparelho realmente usa é preciso ler o blob direto, e é isso que este script
faz: cabeçalho de 10 palavras big-endian, bloco de estrutura
(FDT_BEGIN_NODE / FDT_PROP / FDT_END_NODE / FDT_END) e bloco de strings.

Uso:
    fdt-dump.py <arquivo.dtb> [dts|no CAMINHO|prop CAMINHO PROP|lista]

    fdt-dump.py r35s.dtb dts                 # árvore inteira em .dts
    fdt-dump.py r35s.dtb no /dsi@ff450000/panel@0
    fdt-dump.py r35s.dtb prop /panel@0 compatible
    fdt-dump.py r35s.dtb lista                # filhos da raiz

O caminho usa os nomes de nó do DTB (`/i2c@ff180000/pmic@20`), não os labels do
DTS: num blob compilado os labels já não existem.
"""
import struct
import sys

# Valores do spec (include/uapi/linux/libfdt_env.h / libfdt.h).
# ATENÇÃO: FDT_END_NODE = 2 e FDT_END = 9 — trocar os dois faz o parser parar
# silenciosamente depois do primeiro nó (aconteceu na primeira versão).
TOK_BEGIN, TOK_END_NODE, TOK_PROP, TOK_NOP, TOK_END = 1, 2, 3, 4, 9


class No:
    def __init__(self, nome, pai=None):
        self.nome = nome
        self.pai = pai
        self.props = {}     # nome -> bytes
        self.filhos = []

    def caminho(self):
        if not self.pai:
            return "/"
        return self.pai.caminho().rstrip("/") + "/" + self.nome

    def buscar(self, caminho):
        if caminho in ("", "/"):
            return self
        atual = self
        for parte in caminho.strip("/").split("/"):
            if not parte:
                continue
            achou = None
            for f in atual.filhos:
                if f.nome == parte:
                    achou = f
                    break
            if not achou:
                return None
            atual = achou
        return atual

    def por_phandle(self):
        """Mapa phandle -> nó, para resolver `gpios = <&gpio3 16 1>`."""
        mapa = {}

        def anda(no):
            for k in ("phandle", "linux,phandle"):
                if k in no.props and len(no.props[k]) == 4:
                    mapa[struct.unpack(">I", no.props[k])[0]] = no
            for f in no.filhos:
                anda(f)

        anda(self)
        return mapa


def carregar(caminho_arq):
    d = open(caminho_arq, "rb").read()
    (magic, totalsize, off_struct, off_strings, off_rsv, ver, lastcomp,
     bootcpu, size_strings, size_struct) = struct.unpack(">10I", d[:40])
    if magic != 0xd00dfeed:
        raise SystemExit("%s nao e um DTB (magic %#x)" % (caminho_arq, magic))
    if totalsize != len(d):
        print("aviso: totalsize %#x != tamanho do arquivo %#x"
              % (totalsize, len(d)), file=sys.stderr)
    strings = d[off_strings:off_strings + size_strings]

    def nome_de(off):
        fim = strings.find(b"\x00", off)
        return strings[off:fim].decode("utf-8", "replace")

    raiz, pilha = None, []
    pos = off_struct
    while pos < off_struct + size_struct:
        tok = struct.unpack(">I", d[pos:pos + 4])[0]
        pos += 4
        if tok == TOK_BEGIN:
            # O nome do nó vai até o NUL e depois alinha em 4 bytes.
            fim = d.find(b"\x00", pos)
            no = No(d[pos:fim].decode("utf-8", "replace"),
                    pilha[-1] if pilha else None)
            pos = (fim + 4) & ~3
            if pilha:
                pilha[-1].filhos.append(no)
            else:
                raiz = no
            pilha.append(no)
        elif tok == TOK_END_NODE:
            if pilha:
                pilha.pop()
        elif tok == TOK_PROP:
            tam, nome_off = struct.unpack(">II", d[pos:pos + 8])
            pos += 8
            valor = d[pos:pos + tam]
            pos = (pos + tam + 3) & ~3
            if pilha:
                pilha[-1].props[nome_de(nome_off)] = valor
        elif tok == TOK_NOP:
            continue
        elif tok == TOK_END:
            break
        else:
            raise SystemExit("token desconhecido %d em %#x" % (tok, pos))
    return raiz


def fmt(valor):
    """Formata um valor de propriedade como no .dts."""
    # Só é lista de strings se TODO byte for imprimível ou NUL. Um teste frouxo
    # ("termina em NUL") classificaria <0x0 0xff250000 ...> como texto e
    # embaralharia justamente os `reg` (foi um bug real).
    if valor and valor.endswith(b"\x00") and all((32 <= b < 127) or b == 0 for b in valor):
        partes = [p.decode("utf-8", "replace") for p in valor.split(b"\x00") if p]
        return " , ".join('"%s"' % p for p in partes) if partes else '""'
    if valor and len(valor) % 4 == 0:
        n = struct.unpack(">%dI" % (len(valor) // 4), valor)
        return "<" + " ".join("0x%x" % x for x in n) + ">"
    if not valor:
        return "<>"
    return "<" + " ".join("%02x" % b for b in valor) + ">"


def dts(no, prof=0, saida=None):
    saida = saida if saida is not None else []
    pad = "    " * prof
    if prof == 0:
        modelo = no.props.get("model", b"").replace(b"\x00", b" ")
        saida.append("/dts-v1/;\n")
        saida.append("// %s\n" % modelo.decode("utf-8", "replace").strip())
    if no.nome:
        saida.append("%s%s {\n" % (pad, no.nome))
    for k, v in no.props.items():
        saida.append("%s    %s = %s;\n" % (pad, k, fmt(v)))
    for f in no.filhos:
        dts(f, prof + 1, saida)
    if no.nome:
        saida.append("%s};\n" % pad)
    return saida


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    raiz = carregar(argv[1])
    acao = argv[2] if len(argv) > 2 else "dts"

    if acao == "dts":
        sys.stdout.write("".join(dts(raiz)))
    elif acao == "lista":
        for f in raiz.filhos:
            print(f.nome)
    elif acao == "no":
        no = raiz.buscar(argv[3])
        if not no:
            raise SystemExit("no inexistente: " + argv[3])
        sys.stdout.write("".join(dts(no)))
    elif acao == "prop":
        no = raiz.buscar(argv[3])
        if not no or argv[4] not in no.props:
            raise SystemExit("propriedade inexistente: %s %s" % (argv[3], argv[4]))
        print(fmt(no.props[argv[4]]))
    elif acao == "phandles":
        for ph, no in sorted(raiz.por_phandle().items()):
            print("%#x -> %s" % (ph, no.caminho()))
    else:
        raise SystemExit("acao desconhecida: " + acao)


if __name__ == "__main__":
    main(sys.argv)
