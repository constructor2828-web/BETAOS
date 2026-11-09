"""Build an alternate display profile ISO without changing the normal build."""
from pathlib import Path
import re
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
resolution=sys.argv[1]
if not re.fullmatch(r'[0-9]{3,4}x[0-9]{3,4}',resolution):
    raise SystemExit('Expected WIDTHxHEIGHT')
work=root/'build'/('profile-'+resolution)
(work/'iso/boot').mkdir(parents=True,exist_ok=True)
esp=work/'iso/boot/efi.img'
subprocess.run([sys.executable,str(root/'tools/make_esp.py'),str(root/'build/64/BOOTX64.EFI'),str(esp),resolution],check=True)
iso=work/'BETAOS.iso'
subprocess.run(['genisoimage','-quiet','-R','-J','-V','BETAOS','-eltorito-platform','efi','-b','boot/efi.img','-no-emul-boot','-o',str(iso),str(work/'iso')],check=True)
subprocess.run([sys.executable,str(root/'tools/hybrid_iso.py'),str(iso),str(esp)],check=True)
print(iso)
