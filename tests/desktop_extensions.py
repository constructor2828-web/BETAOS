"""Real PS/2 checks for keymaps, installed-app launcher and file clipboard/search."""
import time
from qemu_support import Machine
m=Machine('desktop-extensions');x,y=500,380

def move(tx,ty):
 global x,y
 while (x,y)!=(tx,ty):
  dx=max(-60,min(60,tx-x));dy=max(-60,min(60,ty-y));m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.025)
def click(tx,ty):
 move(tx,ty);m.hmp('mouse_button 1');time.sleep(.06);m.hmp('mouse_button 0');time.sleep(.08)
try:
 m.key('f2');m.type('write demo.asm ret');m.type('asm demo.asm demo');m.wait('BUILD OK: demo')
 offset=m.log.stat().st_size;m.type('demo');m.wait('PROGRAM RETURN: demo',start=offset)
 m.type('keymap es');m.wait('Keyboard layout: es')
 m.type('echo ',enter=False);m.key('semicolon');m.key('ret')
 assert b'CMD: echo \xf1' in m.log.read_bytes()
 m.key('ctrl-alt-spc');m.wait('KEYMAP: de');m.key('ctrl-alt-spc');m.wait('KEYMAP: fr');m.key('ctrl-alt-spc');m.wait('KEYMAP: us')
 click(780,742);m.wait('KEYMAP: es');m.key('f2');m.type('keymap');m.wait('Keyboard layout: es');m.type('keymap us')
 offset=m.log.stat().st_size
 # Installed list order is hello, then the freshly assembled app demo.
 click(40,742);m.screenshot('start-native-apps');click(460,428)
 m.wait('PROGRAM RETURN: demo',start=offset)
 m.key('f3');click(150,180);m.key('ctrl-c');m.key('ctrl-v');m.wait('FILES: Copy created.')
 m.key('f2');m.type('cat copy01_welcome.txt');m.wait('Welcome to BETAOS')
 m.key('f3');m.key('slash');m.type('demo.asm');m.key('down');m.key('ret');m.wait('UI APP: editor')
 # Opening selected result should pick demo.asm rather than a filtered-out file.
 m.key('ctrl-end');m.key('x');m.key('ctrl-s');m.key('f2');m.type('cat demo.asm');m.wait('\nretx\n')
 m.screenshot('extensions-final')
 print('PASS: ES Latin-1 input, layout cycling, direct native commands, Start native launcher, clipboard and file search')
finally:m.close()
