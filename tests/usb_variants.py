"""Superfloppy/MBR/GPT and FAT16/FAT32 through the actual USB transport."""
from qemu_support import Machine, ROOT
from usb_fixture import make_variant

for bits,table in [(16,'super'),(16,'gpt'),(32,'mbr'),(32,'gpt')]:
    path=make_variant(ROOT/'build'/f'usb-{bits}-{table}.img',bits,table)
    extra=['-device','qemu-xhci,id=xhci','-drive',f'file={path},if=none,id=stick,format=raw,readonly=on','-device','usb-storage,drive=stick,bus=xhci.0']
    m=Machine(f'usb-{bits}-{table}',extra=extra)
    try:
        m.wait('USB STORAGE: 0');m.key('f2');m.type('usb ls');m.wait(f'FILES: USB FAT{bits} / read-only')
        m.wait('USB FILE: Long filename from USB.txt')
        m.type('usb cat Long filename from USB.txt');m.wait('LFN works')
        m.type('usb cd DOCS');m.wait('USB FILE: NESTED.TXT')
        m.type('usb cat NESTED.TXT');m.wait('Nested folder works.')
        m.screenshot(f'files-fat{bits}-{table}')
        print(f'PASS: native USB FAT{bits}/{table}, long filenames, nested directories and contents',flush=True)
    finally:m.close()

# Reset and take ownership of the same controller that firmware used to boot.
path=ROOT/'build/BETAOS64.iso'
extra=['-device','qemu-xhci,id=xhci','-drive',f'file={path},if=none,id=stick,format=raw,readonly=on','-device','usb-storage,drive=stick,bus=xhci.0']
m=Machine('usb-firmware-handoff',disk_only=True,extra=extra)
try:
    m.wait('USB STORAGE: 0');m.key('f2');m.type('usb ls');m.wait('USB FILE: EFI')
    m.type('usb cd EFI');m.wait('USB FILE: BOOT');m.type('usb cd BOOT');m.wait('USB FILE: BOOTX64.EFI')
    print('PASS: USB-only firmware boot and native ownership/readback of the boot USB',flush=True)
finally:m.close()
