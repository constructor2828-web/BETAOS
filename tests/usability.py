"""Exercise real window/file/editor interactions through emulated PS/2 input."""
import time
from qemu_support import Machine

m=Machine('usability')
x,y=500,380

def move(tx,ty):
    global x,y
    while(x,y)!=(tx,ty):
        dx=max(-70,min(70,tx-x));dy=max(-70,min(70,ty-y))
        m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.025)

def button(value):
    m.hmp(f'mouse_button {value}');time.sleep(.06)

def click():
    button(1);button(0)

try:
    m.key('f2')
    move(300,109);button(1);move(340,149);button(0)
    m.wait('WINDOW MOVED: 88 128')
    m.key('f3')
    # File manager retains its own default position after moving the terminal.
    move(150,180);click();m.wait('UI FILE: welcome.txt')
    m.key('alt-tab');m.wait('UI APP: terminal')
    offset=m.log.stat().st_size;m.key('ctrl-w');m.wait('UI APP: files',start=offset)
    m.key('f3');m.key('ctrl-n');m.type('edit.txt');m.wait('DESKTOP CREATED: edit.txt')
    m.key('ret');m.wait('UI APP: editor')
    m.type('alpha');m.type('beta');m.type('gamma',enter=False)
    m.key('ctrl-home');m.key('down');m.key('end');m.key('backspace');m.key('ctrl-z');m.key('ctrl-s')
    m.key('f2');m.type('cat edit.txt');m.wait('\nalpha\nbeta\ngamma\n')
    # Redo remains available after save and switching apps.
    m.key('f6');m.key('ctrl-y');m.key('ctrl-s')
    offset=m.log.stat().st_size
    m.key('f2');m.type('cat edit.txt');m.wait('\nalpha\nbet\ngamma\n',start=offset)
    m.key('f6');m.key('a');m.key('ctrl-end');m.key('ret');m.key('tab');m.key('shift-x');m.key('ctrl-s')
    offset=m.log.stat().st_size
    m.key('f2');m.type('cat edit.txt');m.wait('gamma\n    X\n',start=offset)
    # Guard unsaved edits on both close and opening a different file.
    m.key('f6');m.key('z');m.key('ctrl-w');m.key('esc');m.key('ctrl-s')
    offset=m.log.stat().st_size
    m.key('f2');m.type('cat edit.txt');m.wait('    Xz\n',start=offset)
    m.key('f6');m.key('q');m.key('ctrl-w');m.key('d')
    m.key('f6');m.key('ctrl-s')
    offset=m.log.stat().st_size
    m.key('f2');m.type('cat edit.txt');m.wait('    Xz\n',start=offset)
    assert 'Xzq' not in m.log.read_text()[offset:]
    # File-manager rename and deletion require explicit confirmation.
    m.key('f3')
    move(150,202);click()
    move(730,150);click();m.type('renamed.txt')
    m.key('f2');m.type('ls');m.wait('renamed.txt')
    m.key('f3');m.key('delete');m.key('esc')
    m.key('f2');offset=m.log.stat().st_size;m.type('cat renamed.txt');m.wait('alpha',start=offset)
    m.key('f3');m.key('delete');m.key('ret');m.wait('FILE DELETED: renamed.txt')
    m.key('f2');offset=m.log.stat().st_size;m.type('cat renamed.txt');m.wait('File not found.',start=offset)
    m.screenshot('usability-final')
    print('PASS: per-app drag geometry, Alt-Tab, file toolbar, editor navigation, undo/redo, indentation, unsaved guards, rename/delete confirmation')
finally:
    m.close()
