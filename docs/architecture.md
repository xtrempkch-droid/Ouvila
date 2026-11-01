# Arquitetura

`common/` contém recursos compartilháveis. `devices/` descreve as placas e os artefatos específicos do SoC/board. `variants/` reúne pacotes e overlays da experiência de sistema. `build/` empacota uma combinação de device e variant; o workflow conserva o pipeline de aquisição e compilação existente.

Nesta primeira migração, os serviços e listas de pacotes Kodi foram separados do workflow para `variants/kodi/`. As configurações ROCKNIX e os patches de kernel ainda são baixados no CI, por isso `common/kernel/` e os fragmentos de dispositivo documentam essa limitação sem duplicar fontes externas.

## Arquitetura de hardware

A camada de hardware (SoC, painel, áudio, entrada, rádio, USB) e o mapa de drivers de
cada aparelho estão em **[Hardware e drivers por dispositivo](hardware-e-drivers.md)**.
Esse documento é a referência para portar as placas para outro sistema.

## Variantes

`variants/<variante>/` traz `variant.conf` (id, status, manifesto de pacotes),
`packages/<variante>.list` e o `overlay/` com serviços e arquivos daquela experiência.
Hoje existem duas implementadas — `kodi` (multimídia) e `retroarch` (jogos) — e o
workflow escolhe qual montar pelo input `variant`. O que é comum às duas (rotação e
start do Weston) fica em `common/overlay/usr/local/share/ouvila/display.sh`. Detalhes
em [variantes.md](variantes.md).
