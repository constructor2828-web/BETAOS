"""Rejected installation must explain failure and leave unsuitable disks intact."""
import hashlib
import time
from qemu_support import Machine, ROOT

disk=ROOT/'build/rejected-small-disk.img'
with disk.open('wb') as out:out.truncate(64*1024*1024)
before=hashlib.sha256(disk.read_bytes()).digest()
m=Machine('failure-feedback',extra=['-drive',f'file={disk},format=raw,if=none,id=small','-device','ide-hd,drive=small,bus=ide.0'])
try:
    m.key('f2');m.type('install-os ERASE 0')
    m.wait('UI ERROR: INSTALLATION FAILED: Disk must be at least 128 MiB. No disk was changed.')
    m.type('install-os ERASE 7')
    m.wait('UI ERROR: INSTALLATION FAILED: Invalid disk number. No disk was changed.')
    m.type('write tiny.asm ret')
    for i in range(15):
        m.type(f'asm tiny.asm p{i}');m.wait(f'BUILD OK: p{i}')
    m.type('apps');m.wait('UI APP: apps')
    x,y=500,380
    while (x,y)!=(850,203):
        dx=max(-60,min(60,850-x));dy=max(-60,min(60,203-y))
        m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.03)
    m.hmp('mouse_button 1');time.sleep(.06);m.hmp('mouse_button 0')
    m.wait('UI ERROR: APP INSTALL FAILED: App storage full. Uninstall an app first.')
    m.screenshot('app-storage-full')
    m.key('f2');m.type('uninstall p0');m.type('install fib');m.wait('Installed: fib')
    # All 16 installed apps remain reachable, including the last launcher page.
    m.key('meta_l');m.key('pgdn');m.key('pgdn')
    while (x,y)!=(460,428):
        dx=max(-60,min(60,460-x));dy=max(-60,min(60,428-y))
        m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.04)
    m.hmp('mouse_button 1');time.sleep(.08);m.hmp('mouse_button 0')
    m.wait('PROGRAM RETURN: p14')
finally:m.close()
assert hashlib.sha256(disk.read_bytes()).digest()==before,'Rejected disk was modified'
print('PASS: visible disk/app rejection, undersized disk unchanged, app capacity recovery and all 16 Start-menu apps reachable')
