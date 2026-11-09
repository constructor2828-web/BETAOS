"""Native xHCI + real USB BOT transport, FAT, GUI and BEX integration."""
import hashlib
import time
from qemu_support import Machine, ROOT
from usb_fixture import make_image
p=make_image(ROOT/'build/usb-native.img',large=True);before=hashlib.sha256(p.read_bytes()).digest()
extra=['-device','qemu-xhci,id=xhci','-drive',f'file={p},if=none,id=stick,format=raw,readonly=on','-device','usb-storage,drive=stick,bus=xhci.0,id=flash']
m=Machine('usb-native',extra=extra);x,y=500,380

def click(tx,ty):
 global x,y
 while (x,y)!=(tx,ty):
  dx=max(-60,min(60,tx-x));dy=max(-60,min(60,ty-y));m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.06)
 m.hmp('mouse_button 1');time.sleep(.06);m.hmp('mouse_button 0');time.sleep(.1)
try:
 m.wait('USB STORAGE: 0');m.key('f2');m.type('usb ls');m.wait('USB FILE: Long filename from USB.txt')
 m.type('usb cat README.TXT');m.wait('REAL USB STORAGE / native xHCI FAT16 reader')
 m.type('usb cd DOCS');m.wait('USB FILE: NESTED.TXT');m.type('usb cat NESTED.TXT');m.wait('Nested folder works.')
 m.type('usb back');m.type('usb cp README.TXT');m.wait('Copiado a archivos locales. USB original intacto.')
 m.type('cat README.TXT');m.wait('REAL USB STORAGE / native xHCI FAT16 reader')
 # Select the BEX through Files and install with its toolbar.
 m.key('f3');m.wait('UI APP: files');time.sleep(.15);click(150,224);m.wait('UI USB FILE: FIB.BEX');click(700,150)
 m.wait('Installed: FIB');m.key('f2');m.type('FIB');m.wait('PROGRAM RETURN: FIB')
 m.type('usb install BIG.BEX');m.wait('Installed: BIG');m.type('BIG');m.wait('PROGRAM RETURN: BIG')
 offset=m.log.stat().st_size;m.key('f3');m.wait('UI APP: files',start=offset);time.sleep(.15)
 click(150,180);m.screenshot('files-usb-native')
 # Unplug: a prior FAT selection must not silently read another device.
 m.command('device_del',{'id':'flash'});time.sleep(.25)
 m.key('f2');m.type('usb cat README.TXT');m.wait('USB: lectura interrumpida; comprueba la conexion')
 offset=m.log.stat().st_size;m.type('usb refresh');m.wait('FILES:',start=offset)
 m.command('blockdev-add',{'driver':'raw','node-name':'stick2','read-only':True,'file':{'driver':'file','filename':str(p)}})
 m.command('device_add',{'driver':'usb-storage','drive':'stick2','bus':'xhci.0','id':'flash2'});time.sleep(.2)
 offset=m.log.stat().st_size
 m.type('usb refresh');m.wait('USB STORAGE: 0',start=offset);m.type('usb mount 0');m.type('usb cat README.TXT')
 # Absence of USB writes is additionally enforced by a read-only backend.
 m.wait('REAL USB STORAGE / native xHCI FAT16 reader')
 # More than four sequential connections must recycle disabled slots safely.
 for number in range(3,7):
    m.command('device_del',{'id':f'flash{number-1}'});time.sleep(.25);offset=m.log.stat().st_size;m.type('usb refresh');m.wait('FILES:',start=offset)
    node=f'stick{number}'
    m.command('blockdev-add',{'driver':'raw','node-name':node,'read-only':True,'file':{'driver':'file','filename':str(p)}})
    m.command('device_add',{'driver':'usb-storage','drive':node,'bus':'xhci.0','id':f'flash{number}'})
    offset=m.log.stat().st_size;m.type('usb refresh');m.wait('USB STORAGE: 0',start=offset)
    m.type('usb cat readme.txt');m.wait('REAL USB STORAGE / native xHCI FAT16 reader',start=offset)
 print('PASS: native USB storage, MBR/FAT16/LFN/folders, copy/preview, GUI BEX install, unplug and reconnect')
finally:m.close()
assert hashlib.sha256(p.read_bytes()).digest()==before
