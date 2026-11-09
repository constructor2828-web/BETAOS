"""Install an alternate resolution and confirm its preference survives disk boot."""
from pathlib import Path
import subprocess
import sys
from qemu_support import Machine,ROOT

subprocess.run([sys.executable,str(ROOT/'tools/profile_iso.py'),'800x600'],check=True,stdout=subprocess.DEVNULL)
disk=ROOT/'build/profile-800x600/install.img'
with disk.open('wb') as image:image.truncate(128*1024*1024)
extra=['-drive',f'file={disk},format=raw,if=none,id=target','-device','ide-hd,drive=target,bus=ide.0']
m=Machine('profile-install',ROOT/'build/profile-800x600/BETAOS.iso',extra=extra)
try:
    m.wait('GFX: 800x600');m.key('f2');m.type('install-os ERASE 0');m.wait('INSTALL OK',timeout=60)
finally:m.close()
m=Machine('profile-installed',extra=extra,disk_only=True)
try:
    m.wait('GFX: 800x600');m.wait('PERSISTENT STATE RESTORED');m.key('f2');m.type('echo profile-persisted');m.wait('CMD: echo profile-persisted')
    print('PASS: 800x600 boot preference copied, verified and restored after disk-only UEFI boot')
finally:m.close()
