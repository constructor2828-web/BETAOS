"""Corrupt saved data must be rejected while preserving independent disk boot."""
from pathlib import Path
import shutil
import struct
from qemu_support import Machine,ROOT

original=ROOT/'build/installed-os.img'
assert original.exists(),'Run make test-install first'
for mode in ('crc','oversized','schema'):
    image=ROOT/'build'/f'corrupt-state-{mode}.img';shutil.copyfile(original,image)
    with image.open('r+b') as disk:
        disk.seek(136192*512);header=bytearray(disk.read(512))
        if mode=='crc':header[20]^=1
        elif mode=='oversized':struct.pack_into('<I',header,16,0xffffffff)
        else:
            import binascii
            length=struct.unpack_from('<I',header,16)[0]
            disk.seek(136193*512);state=bytearray(disk.read(length))
            file_bytes=struct.unpack_from('<I',state,4)[0]
            struct.pack_into('<I',state,16+file_bytes,0xffffffff)
            disk.seek(136193*512);disk.write(state)
            struct.pack_into('<I',header,20,binascii.crc32(state))
        disk.seek(136192*512);disk.write(header)
    extra=['-drive',f'file={image},format=raw,if=none,id=target','-device','ide-hd,drive=target,bus=ide.0']
    m=Machine('corrupt-'+mode,extra=extra,disk_only=True)
    try:
        assert 'PERSISTENT STATE RESTORED' not in m.log.read_text()
        m.key('f2');m.type('cat hello');m.wait('File not found.')
        m.type('run fib');m.wait('Program not found.')
        m.type('run hello');m.wait('PROGRAM RETURN: hello')
        assert 'PANIC' not in m.log.read_text()
        print(f'PASS: invalid saved-state {mode} rejected; installed OS still boots and default native app runs')
    finally:m.close()
