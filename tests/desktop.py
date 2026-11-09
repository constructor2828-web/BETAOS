"""Test the actual kernel via QMP pipes; no host listening sockets required."""
import json
import os
import pathlib
import select
import subprocess
import sys
import time
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
usb = '--usb' in sys.argv
install = '--install' in sys.argv
installed = '--installed' in sys.argv
disk_image = BUILD / 'installed-os.img'
if install:
    with disk_image.open('wb') as disk:
        disk.truncate(128 * 1024 * 1024)
mode = 'install' if install else 'installed' if installed else 'usb' if usb else 'optical'
log = BUILD / f'desktop-{mode}-test.log'
log.write_text('')
media = ['-drive', f'file={BUILD / "BETAOS64.iso"},format=raw,if=none,id=usb,readonly=on',
         '-device', 'qemu-xhci', '-device', 'usb-storage,drive=usb'] if usb else [
         '-cdrom', str(BUILD / 'BETAOS64.iso')]
if installed:
    media = []
if install or installed:
    media += ['-drive', f'file={disk_image},format=raw,if=none,id=target', '-device', 'ide-hd,drive=target,bus=ide.0']
pipe_path = BUILD / f'http-fixture-{mode}'
for suffix in ('.in', '.out'):
    path = pathlib.Path(str(pipe_path) + suffix)
    path.unlink(missing_ok=True)
    os.mkfifo(path)
fixture_in = os.open(str(pipe_path) + '.in', os.O_RDWR)
fixture_out = os.open(str(pipe_path) + '.out', os.O_RDWR)

def fixture_server():
    request = bytearray()
    while b'\r\n\r\n' not in request:
        request.extend(os.read(fixture_out, 4096))
    body = b'<h1>BETAOS HTTP TEST</h1><p>Native TCP works &amp; HTML renders.</p>' + b'<p>Scroll test: native browser content.</p>' * 70
    os.write(fixture_in, b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
threading.Thread(target=fixture_server, daemon=True).start()
process = subprocess.Popen([
    'qemu-system-x86_64', '-machine', 'q35', '-m', '512',
    '-bios', os.environ.get('OVMF', '/usr/share/OVMF/x64/OVMF.4m.fd'),
    *media, '-vga', 'std', '-netdev', f'user,id=n0,guestfwd=tcp:10.0.2.100:80-pipe:{pipe_path}',
    '-device', 'rtl8139,netdev=n0', '-object', f'filter-dump,id=capture,netdev=n0,file={BUILD / "network.pcap"}', '-display', 'none', '-serial', f'file:{log}',
    '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
qmp_buffer = bytearray()


def qmp_read(timeout=10):
    deadline = time.monotonic() + timeout
    while b'\n' not in qmp_buffer:
        if time.monotonic() > deadline:
            raise AssertionError('QMP timed out')
        ready, _, _ = select.select([process.stdout], [], [], .1)
        if ready:
            chunk = os.read(process.stdout.fileno(), 65536)
            if not chunk:
                raise AssertionError(process.stderr.read().decode() + log.read_text())
            qmp_buffer.extend(chunk)
    line, _, rest = qmp_buffer.partition(b'\n')
    qmp_buffer[:] = rest
    return json.loads(line)


def command(name, args=None):
    process.stdin.write((json.dumps({'execute': name, 'arguments': args or {}}) + '\n').encode())
    process.stdin.flush()
    while True:
        result = qmp_read()
        if 'return' in result:
            return result['return']
        if 'error' in result:
            raise AssertionError(result)


def wait_for(marker, timeout=35, start=0):
    deadline = time.monotonic() + timeout
    while marker not in log.read_text(errors='replace')[start:]:
        if process.poll() is not None:
            raise AssertionError(process.stderr.read().decode() + log.read_text())
        if time.monotonic() > deadline:
            raise AssertionError(f'Timeout: {marker}\n{log.read_text()}')
        time.sleep(.05)


def key(name):
    command('human-monitor-command', {'command-line': f'sendkey {name} 20'})
    time.sleep(.04)


def type_text(text):
    special = {':': 'shift-semicolon', '/': 'slash', '.': 'dot', ' ': 'spc', '-': 'minus'}
    for char in text:
        key('shift-' + char.lower() if char.isupper() else special.get(char, char))
    key('ret')


mouse_x, mouse_y = 500, 380

def move_mouse(x, y):
    global mouse_x, mouse_y
    while (mouse_x, mouse_y) != (x, y):
        dx = max(-80, min(80, x-mouse_x))
        dy = max(-80, min(80, y-mouse_y))
        command('human-monitor-command', {'command-line': f'mouse_move {dx} {dy}'})
        mouse_x += dx
        mouse_y += dy
        time.sleep(.04)


def click():
    command('human-monitor-command', {'command-line': 'mouse_button 1'})
    time.sleep(.06)
    command('human-monitor-command', {'command-line': 'mouse_button 0'})
    time.sleep(.06)


try:
    qmp_read()
    command('qmp_capabilities')
    wait_for('DESKTOP READY')
    wait_for('DHCP configured')
    if installed:
        wait_for('PERSISTENT STATE RESTORED')
        wait_for('DESKTOP LAYOUT RESTORED')
        key('f2')
        type_text('cat hello')
        wait_for('\nnative-files\n')
        type_text('ls')
        wait_for('notes2.txt')
        type_text('type notes2.txt')
        wait_for('\ndesktop text\n')
        type_text('run scratch')
        wait_for('Hello from the built-in assembler!')
        wait_for('PROGRAM RETURN: scratch')
        type_text('run fib')
        wait_for('0 1 1 2 3 5 8 13 21 34 55 89')
        type_text('halt')
        wait_for('HALTED')
        print('PASS: installed hard-disk boot WITHOUT ISO, restored files, compiled app and installed apps')
        sys.exit(0)
    # Desktop context menu: create, change icon, rename, and drag a file.
    move_mouse(600,200)
    command('human-monitor-command', {'command-line': 'mouse_button 2'})
    time.sleep(.06)
    command('human-monitor-command', {'command-line': 'mouse_button 0'})
    move_mouse(630,220)
    click()
    type_text('notes.txt')
    wait_for('DESKTOP CREATED: notes.txt')
    move_mouse(650,230)
    command('human-monitor-command', {'command-line': 'mouse_button 2'})
    time.sleep(.06)
    command('human-monitor-command', {'command-line': 'mouse_button 0'})
    move_mouse(700,310)
    click()
    wait_for('DESKTOP ICON CHANGED')
    move_mouse(650,230)
    command('human-monitor-command', {'command-line': 'mouse_button 2'})
    time.sleep(.06)
    command('human-monitor-command', {'command-line': 'mouse_button 0'})
    move_mouse(700,280)
    click()
    type_text('notes2.txt')
    move_mouse(650,230)
    command('human-monitor-command', {'command-line': 'mouse_button 1'})
    time.sleep(.06)
    move_mouse(730,330)
    command('human-monitor-command', {'command-line': 'mouse_button 0'})
    wait_for('DESKTOP MOVED')
    command('screendump', {'filename': str(BUILD / 'home.ppm')})
    move_mouse(196, 742)
    click()
    wait_for('UI APP: terminal')
    type_text('about')
    wait_for('CMD: about')
    move_mouse(914,109)
    click()
    wait_for('WINDOW MAXIMIZED')
    move_mouse(964,21)
    click()
    wait_for('WINDOW RESTORED')
    move_mouse(881,109)
    click()
    wait_for('UI APP: desktop')
    key('f2')
    type_text('programs')
    wait_for('NATIVE X86-64 PROGRAMS / BEX ABI 1')
    type_text('run hello')
    wait_for('Hello from a native x86-64 assembly program!')
    wait_for('PROGRAM RETURN: hello')
    type_text('install fib')
    wait_for('Installed: fib')
    type_text('run fib')
    wait_for('0 1 1 2 3 5 8 13 21 34 55 89')
    wait_for('PROGRAM RETURN: fib')
    type_text('install memory')
    wait_for('Installed: memory')
    type_text('run memory')
    wait_for('PROGRAM RETURN: memory')
    type_text('run missing')
    wait_for('Program not found.')
    command('screendump', {'filename': str(BUILD / 'native-programs.ppm')})
    key('esc')
    move_mouse(178, 57)
    offset = len(log.read_text())
    click()
    click()
    wait_for('PROGRAM RETURN: hello', start=offset)

    type_text('write hello native-files')
    wait_for('CMD: write hello native-files')
    type_text('cat hello')
    wait_for('CMD: cat hello')
    wait_for('\nnative-files\n')
    # App switching retains an unfinished terminal command.
    for char in 'echo retained':
        key('spc' if char == ' ' else char)
    key('f3')
    key('f2')
    key('ret')
    wait_for('CMD: echo retained')
    key('esc')
    wait_for('UI APP: desktop')
    move_mouse(50, 742)
    click()
    move_mouse(150, 500)
    click()
    wait_for('UI APP: files')
    move_mouse(150, 180)
    click()
    wait_for('UI FILE: welcome.txt')
    command('screendump', {'filename': str(BUILD / 'files.ppm')})
    key('f1')
    type_text('http://10.0.2.100/')
    wait_for('HTTP OK: 10.0.2.100', timeout=20)
    wait_for('Native TCP works & HTML renders.')
    key('pgdn')
    wait_for('UI SCROLL: 5')
    key('pgup')
    wait_for('UI SCROLL: 0')
    time.sleep(.2)
    registers = command('human-monitor-command', {'command-line': 'info registers'})
    assert 'CS64' in registers, registers
    command('screendump', {'filename': str(BUILD / 'desktop.ppm')})
    key('f2')
    type_text('apps')
    wait_for('UI APP: apps')
    command('screendump', {'filename': str(BUILD / 'app-installer.ppm')})
    move_mouse(850,299)
    click()
    wait_for('Uninstalled: memory')
    click()
    key('f5')
    wait_for('UI APP: gears')
    time.sleep(.2)
    command('screendump', {'filename': str(BUILD / 'gears-moving.ppm')})
    time.sleep(.3)
    key('spc')
    time.sleep(.2)
    command('screendump', {'filename': str(BUILD / 'gears.ppm')})
    def gear_pixels(path):
        header, dimensions, depth, pixels = path.read_bytes().split(b'\n', 3)
        width, height = map(int, dimensions.split())
        assert header == b'P6' and depth == b'255'
        return b''.join(pixels[(y*width+250)*3:(y*width+750)*3] for y in range(200,600))
    assert gear_pixels(BUILD / 'gears-moving.ppm') != gear_pixels(BUILD / 'gears.ppm'), 'Gears did not animate' 
    key('f6')
    wait_for('UI APP: editor')
    command('screendump', {'filename': str(BUILD / 'editor.ppm')})
    key('ctrl-s')
    wait_for('EDITOR SAVED')
    key('ctrl-r')
    wait_for('ASSEMBLER BUILD OK')
    wait_for('Hello from the built-in assembler!')
    wait_for('PROGRAM RETURN: scratch')
    # Open the desktop-created file with a double click and edit it as text.
    key('esc')
    move_mouse(730,330)
    click()
    click()
    offset=len(log.read_text())
    type_text('desktop text')
    key('ctrl-s')
    wait_for('EDITOR SAVED',start=offset)
    key('f2')
    type_text('type notes2.txt')
    wait_for('\ndesktop text\n')
    type_text('edit scratch.asm')
    key('f2')
    type_text('help')
    wait_for('CMD: help')
    for char in 'echo abc':
        key('spc' if char == ' ' else char)
    key('left')
    key('left')
    key('left')
    key('d')
    key('ret')
    wait_for('CMD: echo dabc')
    key('e')
    key('c')
    key('tab')
    type_text('complete')
    wait_for('CMD: echo complete')
    offset=len(log.read_text())
    key('up')
    key('ret')
    wait_for('CMD: echo complete',start=offset)
    type_text('dir')
    wait_for('CMD: dir')
    type_text('type hello')
    wait_for('CMD: type hello')
    if install:
        type_text('disks')
        wait_for('disk 0 / 128 MiB')
        type_text('install-os')
        wait_for('To confirm: install-os ERASE N')
        type_text('installer')
        wait_for('UI APP: installer')
        command('screendump', {'filename': str(BUILD / 'disk-installer.ppm')})
        move_mouse(200,200)
        click()
        move_mouse(200,600)
        click()
        with disk_image.open('rb') as target:
            assert target.read(512) == bytes(512), 'Installer wrote before erase confirmation'
        move_mouse(100,541)
        click()
        move_mouse(200,600)
        click()
        wait_for('INSTALL OK', timeout=60)
        command('screendump', {'filename': str(BUILD / 'installer.ppm')})
    key('f2')
    type_text('halt')
    wait_for('HALTED')
    print(f'PASS: {"USB GPT" if usb else "ISO optical"} UEFI boot, x86-64, ExitBootServices, taskbar, desktop create/rename/icon/drag, native apps, editor/assembler, scrolling, TCP/HTTP, halt')
finally:
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    errors = process.stderr.read().decode()
    errors = '\n'.join(line for line in errors.splitlines() if 'terminating on signal 15' not in line)
    if errors:
        print(errors)
    os.close(fixture_in)
    os.close(fixture_out)
    for suffix in ('.in', '.out'):
        pathlib.Path(str(pipe_path) + suffix).unlink(missing_ok=True)
