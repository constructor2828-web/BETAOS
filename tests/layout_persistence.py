"""Keyboard settings must survive disk-only native boot; old schema still loads."""
import shutil
from qemu_support import Machine, ROOT

disk=ROOT/'build/layout-persistence.img'
shutil.copyfile(ROOT/'build/installed-os.img',disk)
extra=['-drive',f'file={disk},format=raw,if=none,id=system','-device','ide-hd,drive=system,bus=ide.0']
m=Machine('layout-save',extra=extra)
try:
    m.key('f2');m.type('keymap es');m.wait('Keyboard layout: es')
    m.type('save');m.wait('State saved to disk.')
finally:m.close()
m=Machine('layout-restored',disk_only=True,extra=extra)
try:
    m.key('f2');m.type('keymap');m.wait('Keyboard layout: es')
    m.type('echo ',enter=False);m.key('semicolon');m.key('ret')
    assert b'CMD: echo \xf1' in m.log.read_bytes()
    print('PASS: old state compatibility, ES setting saved and restored on disk-only boot')
finally:m.close()
