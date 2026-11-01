# -*- coding: utf-8 -*-
"""Painel de Wi-Fi e Bluetooth dentro do Kodi (Powkiddy X55 / RK3566).

O Kodi nao tem gerenciador de radio no Linux (o de Wi-Fi existe apenas nos
builds Windows/Android), entao este add-on fala direto com o NetworkManager
(`nmcli`) e o BlueZ (`bluetoothctl`), os mesmos programas que a imagem instala.
"""
import os
import re
import shutil
import subprocess
import threading
import time

import xbmc
import xbmcgui

TITULO = 'Ouvila Radio'

# O servico do Kodi roda com HOME=/root e o PATH do systemd; usar caminho
# absoluto evita depender de como o Kodi foi iniciado.
def _achar(nome, alternativas):
    caminho = shutil.which(nome)
    if caminho:
        return caminho
    for alt in alternativas:
        if os.path.exists(alt):
            return alt
    return None


NMCLI = _achar('nmcli', ['/usr/bin/nmcli', '/bin/nmcli'])
BTCTL = _achar('bluetoothctl', ['/usr/bin/bluetoothctl', '/bin/bluetoothctl'])
SYSTEMCTL = _achar('systemctl', ['/usr/bin/systemctl', '/bin/systemctl'])

# O scan interativo imprime "[NEW] Device MAC nome"; `bluetoothctl devices`
# imprime "Device MAC nome". Por isso a busca nao e ancorada no inicio.
RE_DEVICE = re.compile(r'Device\s+([0-9A-Fa-f:]{17})\s*(.*)$')

# Teclado do xbmcgui. Medido no Kodi 20 do aparelho:
#   input(type=0, option=0)   -> teclado alfanumerico, aceita "abC123xy"
#   input(type=0, option=64)  -> idem, com os caracteres ocultos
#   input(type=64)            -> o teclado NAO abre (retorna vazio)
# Ou seja: ALPHANUM_HIDE_INPUT e uma *opcao*, nao um tipo. Passar 64 no `type`
# (o bug antigo) dava a impressao de "so aceita numeros".
INPUT_ALPHANUM = getattr(xbmcgui, 'INPUT_ALPHANUM', 0)
ALPHANUM_HIDE_INPUT = getattr(xbmcgui, 'ALPHANUM_HIDE_INPUT', 64)
SEGURANCA_ABERTA = ('', '--', 'none', 'open')


# --------------------------------------------------------------------------
# utilidades
# --------------------------------------------------------------------------
def run(args, timeout=25):
    """Roda um comando sem shell e devolve (codigo, saida_completa)."""
    if not args or not args[0]:
        return 127, 'comando nao encontrado'
    try:
        p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, encoding='utf-8', errors='replace')
        return p.returncode, p.stdout or ''
    except subprocess.TimeoutExpired:
        return 124, 'tempo esgotado'
    except OSError as exc:
        return 126, str(exc)


def notificar(msg, ok=True, ms=4000):
    xbmcgui.Dialog().notification(
        TITULO, msg,
        xbmcgui.NOTIFICATION_INFO if ok else xbmcgui.NOTIFICATION_ERROR, ms)


def mostrar(texto):
    xbmcgui.Dialog().textviewer(TITULO, texto)


def dividir_nmcli(linha):
    """Separa uma linha de `nmcli -t`, respeitando os escapes `\\:` e `\\\\`."""
    campos, atual, escape = [], [], False
    for ch in linha:
        if escape:
            atual.append(ch)
            escape = False
        elif ch == '\\':
            escape = True
        elif ch == ':':
            campos.append(''.join(atual))
            atual = []
        else:
            atual.append(ch)
    campos.append(''.join(atual))
    return campos


# --------------------------------------------------------------------------
# Wi-Fi (NetworkManager)
# --------------------------------------------------------------------------
def wifi_estado():
    """(estado, conexao, ip) do wlan0."""
    estado, conexao, ip = None, '', ''
    _, saida = run([NMCLI, '-t', '-f', 'DEVICE,STATE,CONNECTION', 'dev', 'status'], 15)
    for linha in saida.splitlines():
        campos = dividir_nmcli(linha)
        if campos and campos[0] == 'wlan0':
            estado = campos[1] if len(campos) > 1 else ''
            conexao = campos[2] if len(campos) > 2 else ''
            break
    _, saida = run([NMCLI, '-t', '-f', 'IP4.ADDRESS', 'dev', 'show', 'wlan0'], 15)
    for linha in saida.splitlines():
        campos = dividir_nmcli(linha)
        if len(campos) >= 2 and campos[0] == 'IP4.ADDRESS[1]':
            ip = campos[1]
            break
    return estado, conexao, ip


def wifi_ligado():
    _, saida = run([NMCLI, 'radio', 'wifi'], 10)
    return saida.strip() == 'enabled'


def wifi_redes():
    """Lista de dicts {'ssid','signal','security','in_use'} sem duplicatas."""
    # rescan pode falhar se ja existe um em andamento: nao e erro.
    run([NMCLI, 'dev', 'wifi', 'rescan'], 20)
    time.sleep(4)
    _, saida = run([NMCLI, '-t', '-f', 'IN-USE,SSID,SIGNAL,SECURITY',
                    'dev', 'wifi', 'list'], 25)
    redes = {}
    for linha in saida.splitlines():
        campos = dividir_nmcli(linha)
        if len(campos) < 4:
            continue
        em_uso, ssid, sinal, seguranca = campos[0], campos[1], campos[2], campos[3]
        if not ssid:  # rede oculta: o nmcli nao entrega o nome aqui
            continue
        try:
            valor = int(sinal)
        except ValueError:
            valor = 0
        anterior = redes.get(ssid)
        if anterior is None or valor > anterior['signal']:
            redes[ssid] = {'ssid': ssid, 'signal': valor,
                           'security': seguranca,
                           'in_use': em_uso.strip() == '*'}
    return sorted(redes.values(), key=lambda r: (-r['signal'], r['ssid'].lower()))


def pedir_senha(ssid, visivel=False):
    """Abre o teclado alfanumerico (letras + numeros) para a senha do Wi-Fi.

    Por padrao os caracteres ficam ocultos; com visivel=True eles aparecem, o
    que ajuda a conferir o que foi digitado no controle do aparelho.
    """
    opcao = 0 if visivel else ALPHANUM_HIDE_INPUT
    titulo = 'Senha de %s' % ssid
    if visivel:
        titulo += ' (visivel)'
    return xbmcgui.Dialog().input(titulo, type=INPUT_ALPHANUM, option=opcao)


def wifi_conectar(ssid, senha=None):
    args = [NMCLI, 'dev', 'wifi', 'connect', ssid]
    if senha:
        args += ['password', senha]
    return run(args, 60)


def wifi_desconectar():
    return run([NMCLI, 'dev', 'disconnect', 'wlan0'], 25)


def menu_wifi_status():
    dialogo = xbmcgui.Dialog()
    estado, conexao, ip = wifi_estado()
    linhas = []
    if estado is None:
        linhas.append('Adaptador wlan0 nao encontrado.')
        linhas.append('')
        linhas.append('Verifique com: dmesg | grep rtw88')
        linhas.append('A imagem tem um servico que recarrega o modulo no boot:')
        linhas.append('  systemctl status ouvila-radio-retry')
        mostrar('\n'.join(linhas))
        return
    linhas.append('Radio: %s' % ('ligado' if wifi_ligado() else 'desligado'))
    linhas.append('wlan0: %s' % estado)
    linhas.append('Rede: %s' % (conexao or '(nenhuma)'))
    linhas.append('IP: %s' % (ip or '(sem IP)'))
    mostrar('\n'.join(linhas))

    opcoes = ['Procurar redes', 'Desconectar']
    if not wifi_ligado():
        opcoes = ['Ligar o radio Wi-Fi'] + opcoes
    escolha = dialogo.select(TITULO, opcoes)
    if escolha < 0:
        return
    opcao = opcoes[escolha]
    if opcao == 'Ligar o radio Wi-Fi':
        run([NMCLI, 'radio', 'wifi', 'on'], 20)
        notificar('Radio Wi-Fi ligado')
    elif opcao == 'Procurar redes':
        menu_wifi_redes()
    else:
        codigo, saida = wifi_desconectar()
        notificar('Desconectado' if codigo == 0 else 'Falha: %s' % saida[:120],
                  codigo == 0)


def menu_wifi_redes():
    dialogo = xbmcgui.Dialog()
    if not wifi_ligado():
        run([NMCLI, 'radio', 'wifi', 'on'], 20)
    progresso = xbmcgui.DialogProgress()
    progresso.create(TITULO, 'Procurando redes Wi-Fi...')
    progresso.update(30)
    redes = wifi_redes()
    progresso.close()
    if not redes:
        notificar('Nenhuma rede encontrada', ok=False)
        return
    rotulos = []
    for rede in redes:
        marca = ' [conectada]' if rede['in_use'] else ''
        aberta = rede['security'].strip().lower() in SEGURANCA_ABERTA
        rotulos.append('%s  (%d%%)%s%s' % (
            rede['ssid'], rede['signal'], '' if aberta else '  ' + rede['security'],
            marca))
    escolha = dialogo.select('Redes Wi-Fi', rotulos)
    if escolha < 0:
        return
    rede = redes[escolha]
    if rede['in_use']:
        codigo, saida = wifi_desconectar()
        notificar('Desconectado' if codigo == 0 else 'Falha: %s' % saida[:120],
                  codigo == 0)
        return
    senha = None
    protegida = rede['security'].strip().lower() not in SEGURANCA_ABERTA
    if protegida:
        senha = pedir_senha(rede['ssid'])
        if senha is None:
            return
        if not senha:
            notificar('Senha vazia', ok=False)
            return
    progresso.create(TITULO, 'Conectando em %s...' % rede['ssid'])
    progresso.update(50)
    codigo, saida = wifi_conectar(rede['ssid'], senha)
    progresso.close()
    if codigo == 0:
        _, _, ip = wifi_estado()
        notificar('Conectado em %s' % rede['ssid'])
        if ip:
            notificar('IP: %s' % ip, ms=6000)
        return
    # Digitar senha no controle erra facil: deixa tentar de novo vendo os
    # caracteres antes de mostrar o erro do nmcli.
    if protegida and dialogo.yesno(TITULO, 'Nao conectou em %s.\n\nDigitar a senha '
                                   'de novo, agora com os caracteres visiveis?'
                                   % rede['ssid']):
        senha = pedir_senha(rede['ssid'], visivel=True)
        if senha is None or not senha:
            return
        progresso.create(TITULO, 'Conectando em %s...' % rede['ssid'])
        progresso.update(50)
        codigo, saida = wifi_conectar(rede['ssid'], senha)
        progresso.close()
        if codigo == 0:
            _, _, ip = wifi_estado()
            notificar('Conectado em %s' % rede['ssid'])
            if ip:
                notificar('IP: %s' % ip, ms=6000)
            return
    mostrar('Falha ao conectar em %s.\n\n%s' % (rede['ssid'], saida.strip()[:800]))


# --------------------------------------------------------------------------
# Bluetooth (BlueZ)
# --------------------------------------------------------------------------
def bt_info():
    """Dados do controlador: {'controller','Name','Powered',...}."""
    _, saida = run([BTCTL, 'show'], 20)
    info = {}
    for linha in saida.splitlines():
        linha = linha.strip()
        if linha.startswith('Controller '):
            partes = linha.split()
            if len(partes) > 1:
                info['controller'] = partes[1]
        elif ':' in linha:
            chave, valor = linha.split(':', 1)
            info[chave.strip()] = valor.strip()
    return info


def bt_ligado():
    return bt_info().get('Powered', '').lower() == 'yes'


def bt_listar(filtro=None):
    args = [BTCTL, 'devices'] + ([filtro] if filtro else [])
    _, saida = run(args, 20)
    itens = []
    for linha in saida.splitlines():
        casou = RE_DEVICE.search(linha.strip())
        if casou:
            mac = casou.group(1).upper()
            nome = casou.group(2).strip() or mac
            itens.append({'mac': mac, 'name': nome})
    return itens


def bt_escanear(segundos=12):
    """Roda `bluetoothctl --timeout N scan on` em thread, com progresso na tela."""
    encontrados, trava = {}, threading.Lock()

    def coletor():
        try:
            processo = subprocess.Popen(
                [BTCTL, '--timeout', str(segundos), 'scan', 'on'],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                encoding='utf-8', errors='replace', bufsize=1)
        except OSError:
            return
        for linha in processo.stdout:
            casou = RE_DEVICE.search(linha.strip())
            if not casou:
                continue
            mac = casou.group(1).upper()
            # Só a linha "[NEW] Device ..." traz o nome do aparelho; as demais
            # ("[CHG] ... RSSI: -60") só confirmam que ele existe.
            nome = casou.group(2).strip() if linha.lstrip().startswith('[NEW]') else ''
            with trava:
                if mac not in encontrados:
                    encontrados[mac] = ''
                if nome:
                    encontrados[mac] = nome
        try:
            processo.wait(timeout=10)
        except subprocess.TimeoutExpired:
            processo.kill()

    progresso = xbmcgui.DialogProgress()
    progresso.create(TITULO, 'Procurando dispositivos Bluetooth...')
    thread = threading.Thread(target=coletor)
    thread.daemon = True
    thread.start()
    inicio = time.time()
    while thread.is_alive():
        if progresso.iscanceled():
            break
        decorrido = time.time() - inicio
        with trava:
            quantidade = len(encontrados)
        progresso.update(int(min(99, decorrido * 100 / segundos)),
                         'Procurando dispositivos Bluetooth...',
                         '%d encontrado(s)' % quantidade)
        xbmc.sleep(250)
    progresso.close()
    # O BlueZ guarda o nome dos aparelhos descobertos: completa o que faltar.
    conhecidos = {d['mac']: d['name'] for d in bt_listar()}
    with trava:
        itens = [{'mac': mac, 'name': nome or conhecidos.get(mac) or mac}
                 for mac, nome in encontrados.items()]
    return sorted(itens, key=lambda d: d['name'].lower())


def menu_bt_status():
    dialogo = xbmcgui.Dialog()
    info = bt_info()
    linhas = []
    if not info.get('controller'):
        linhas.append('Nenhum controlador Bluetooth encontrado.')
        linhas.append('')
        linhas.append('O hci0 sobe no boot; se falhar, o servico de retry tenta')
        linhas.append('de novo. Verifique:')
        linhas.append('  dmesg | grep -i "Bluetooth: hci0"')
        linhas.append('  systemctl status bluetooth')
        mostrar('\n'.join(linhas))
        escolha = dialogo.select(TITULO, ['Reiniciar o servico Bluetooth'])
        if escolha == 0 and SYSTEMCTL:
            run([SYSTEMCTL, 'restart', 'bluetooth'], 40)
            notificar('bluetooth reiniciado')
        return
    linhas.append('Controlador: %s' % info.get('controller', '?'))
    linhas.append('Nome: %s' % info.get('Name', info.get('Alias', '?')))
    linhas.append('Ligado: %s' % ('sim' if info.get('Powered', '').lower() == 'yes' else 'nao'))
    linhas.append('Visivel: %s' % ('sim' if info.get('Discoverable', '').lower() == 'yes' else 'nao'))
    mostrar('\n'.join(linhas))

    ligado = info.get('Powered', '').lower() == 'yes'
    opcoes = ['Desligar o Bluetooth', 'Procurar dispositivos', 'Pareados',
              'Ficar visivel por 2 minutos', 'Reiniciar o servico Bluetooth']
    if not ligado:
        opcoes = ['Ligar o Bluetooth'] + opcoes
    escolha = dialogo.select(TITULO, opcoes)
    if escolha < 0:
        return
    opcao = opcoes[escolha]
    if opcao == 'Ligar o Bluetooth':
        run([BTCTL, 'power', 'on'], 25)
        notificar('Bluetooth ligado')
    elif opcao == 'Desligar o Bluetooth':
        run([BTCTL, 'power', 'off'], 25)
        notificar('Bluetooth desligado')
    elif opcao == 'Procurar dispositivos':
        menu_bt_procurar()
    elif opcao == 'Pareados':
        menu_bt_pareados()
    elif opcao == 'Ficar visivel por 2 minutos':
        run([BTCTL, 'discoverable', 'on'], 20)
        run([BTCTL, 'pairable', 'on'], 20)
        notificar('Visivel por 2 minutos', ms=5000)
        xbmc.sleep(120000)
        run([BTCTL, 'discoverable', 'off'], 20)
    else:
        if SYSTEMCTL:
            run([SYSTEMCTL, 'restart', 'bluetooth'], 40)
            notificar('bluetooth reiniciado')


def bt_parear(mac):
    codigo, saida = run([BTCTL, '--timeout', '30', 'pair', mac], 45)
    texto = saida.lower()
    if 'already exists' in texto or 'already paired' in texto:
        return True, saida
    return codigo == 0 and 'pairing successful' in texto, saida


def menu_bt_procurar():
    dialogo = xbmcgui.Dialog()
    if not bt_ligado():
        run([BTCTL, 'power', 'on'], 25)
    dispositivos = bt_escanear()
    if not dispositivos:
        notificar('Nenhum dispositivo encontrado', ok=False)
        return
    pareados = {d['mac'] for d in bt_listar('Paired')}
    rotulos = ['%s  %s' % (d['name'], '' if d['mac'] in pareados else '(novo)')
               for d in dispositivos]
    escolha = dialogo.select('Dispositivos Bluetooth', rotulos)
    if escolha < 0:
        return
    alvo = dispositivos[escolha]
    progresso = xbmcgui.DialogProgress()
    progresso.create(TITULO, 'Pareando com %s...' % alvo['name'])
    progresso.update(30)
    if alvo['mac'] not in pareados:
        ok, saida = bt_parear(alvo['mac'])
        if not ok:
            progresso.close()
            mostrar('Falha ao parear com %s.\n\n%s'
                    % (alvo['name'], saida.strip()[:800]))
            return
    run([BTCTL, 'trust', alvo['mac']], 25)
    progresso.update(70, 'Conectando em %s...' % alvo['name'])
    codigo, saida = run([BTCTL, '--timeout', '25', 'connect', alvo['mac']], 40)
    progresso.close()
    conectou = codigo == 0 and 'successful' in saida.lower()
    notificar('%s conectado' % alvo['name'] if conectou
              else 'Pareado, mas sem conectar: %s' % saida.strip()[:100],
              ok=conectou, ms=6000)


def menu_bt_pareados():
    dialogo = xbmcgui.Dialog()
    pareados = bt_listar('Paired')
    if not pareados:
        notificar('Nenhum dispositivo pareado', ok=False)
        return
    conectados = {d['mac'] for d in bt_listar('Connected')}
    rotulos = ['%s  %s%s' % (d['name'], d['mac'],
                             '  [conectado]' if d['mac'] in conectados else '')
               for d in pareados]
    escolha = dialogo.select('Dispositivos pareados', rotulos)
    if escolha < 0:
        return
    alvo = pareados[escolha]
    if alvo['mac'] in conectados:
        codigo, saida = run([BTCTL, 'disconnect', alvo['mac']], 30)
        notificar('Desconectado' if codigo == 0 else 'Falha: %s' % saida[:120],
                  codigo == 0)
        return
    codigo, saida = run([BTCTL, '--timeout', '25', 'connect', alvo['mac']], 40)
    notificar('Conectado em %s' % alvo['name'] if codigo == 0
              else 'Falha: %s' % saida.strip()[:120],
              ok=codigo == 0)


# --------------------------------------------------------------------------
# entrada
# --------------------------------------------------------------------------
AJUDA = (
    'Este add-on nao substitui o Kodi: ele apenas chama o NetworkManager\n'
    '(Wi-Fi) e o BlueZ (Bluetooth), que ja vem na imagem.\n\n'
    'Wi-Fi: procurar redes, conectar com senha e desconectar.\n'
    'Bluetooth: ligar/desligar, procurar, parear, conectar e ficar visivel.\n\n'
    'Se o Wi-Fi ou o Bluetooth nao aparecerem, o servico de retry do sistema\n'
    'recarrega o modulo no boot. Para ver o que aconteceu:\n'
    '  /boot/radio-retry.log\n'
    '  dmesg | grep -i "Bluetooth: hci0"\n'
    '  dmesg | grep rtw88\n\n'
    'Tambem existe um painel web na mesma rede:\n'
    '  http://10.55.0.1:8088   (pelo cabo USB)\n'
)


def main():
    if not NMCLI and not BTCTL:
        mostrar('Nem o nmcli nem o bluetoothctl foram encontrados.\n'
                'Esta imagem nao tem o NetworkManager/BlueZ instalados.')
        return
    dialogo = xbmcgui.Dialog()
    while True:
        opcoes = []
        if NMCLI:
            opcoes += ['Wi-Fi: status e conexao', 'Wi-Fi: procurar redes']
        if BTCTL:
            opcoes += ['Bluetooth: status', 'Bluetooth: procurar dispositivos',
                       'Bluetooth: pareados']
        opcoes += ['Ajuda', 'Sair']
        escolha = dialogo.select(TITULO, opcoes)
        if escolha < 0 or opcoes[escolha] == 'Sair':
            return
        opcao = opcoes[escolha]
        if opcao == 'Wi-Fi: status e conexao':
            menu_wifi_status()
        elif opcao == 'Wi-Fi: procurar redes':
            menu_wifi_redes()
        elif opcao == 'Bluetooth: status':
            menu_bt_status()
        elif opcao == 'Bluetooth: procurar dispositivos':
            menu_bt_procurar()
        elif opcao == 'Bluetooth: pareados':
            menu_bt_pareados()
        else:
            mostrar(AJUDA)


if __name__ == '__main__':
    main()
