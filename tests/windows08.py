"""Real overlapping windows, focus, minimize/restore, resizing and closing."""
import time
from qemu_support import Machine
m=Machine('windows08');x,y=500,380
def move(tx,ty):
 global x,y
 while (x,y)!=(tx,ty):
  dx=max(-60,min(60,tx-x));dy=max(-60,min(60,ty-y));m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.02)
def button(value):m.hmp(f'mouse_button {value}');time.sleep(.07)
def click():button(1);button(0)
try:
 m.key('f2');move(300,109);button(1);move(340,149);button(0);m.wait('WINDOW MOVED: 88 128')
 m.key('f3');time.sleep(.2);m.screenshot('desktop08-windows')
 offset=m.log.stat().st_size;move(990,350);click();m.wait('UI APP: terminal',start=offset)
 move(920,149);offset=m.log.stat().st_size;click();m.wait('UI APP: files',start=offset)
 move(196,741);click();move(1008,680);button(1);move(850,580);button(0);m.wait('WINDOW RESIZED')
 time.sleep(.2);m.screenshot('desktop08-resized')
 m.key('f1');offset=m.log.stat().st_size;m.key('ctrl-w');m.wait('UI APP: terminal',start=offset)
 offset=m.log.stat().st_size;m.key('ctrl-w');m.wait('UI APP: files',start=offset)
 offset=m.log.stat().st_size;m.key('ctrl-w');m.wait('UI APP: desktop',start=offset)
 print('PASS: overlapping windows, exposed-window focus, minimize/restore, resize and close reveals previous window')
finally:m.close()
