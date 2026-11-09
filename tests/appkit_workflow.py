"""Build a new app, install it through USB BOT, and exercise editor changes."""
import struct
import subprocess
import tempfile
from pathlib import Path
from qemu_support import Machine, ROOT
from usb_fixture import make_image

with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temp:
    project = Path(temp) / 'kitdemo'
    for operation in ('new', 'build'):
        subprocess.run(['python3', 'tools/appkit.py', operation, str(project)], cwd=ROOT, check=True)
    blob = (project / 'kitdemo.bex').read_bytes()
    assert len(blob) <= 1024
    path = make_image(Path(temp) / 'usb.img')
    image = bytearray(path.read_bytes())
    root = (2048 + 257) * 512 + 192
    image[root:root+11] = b'KITDEMO BEX'
    image[root+11] = 32
    struct.pack_into('<H', image, root+26, 8)
    struct.pack_into('<I', image, root+28, len(blob))
    data = (2048 + 289 + (8-2)*2)*512
    image[data:data+len(blob)] = blob
    path.write_bytes(image)
    m = Machine('appkit-workflow', extra=['-device','qemu-xhci,id=xhci','-drive',
        f'file={path},if=none,id=stick,format=raw,readonly=on',
        '-device','usb-storage,drive=stick,bus=xhci.0'])
    try:
        m.wait('USB STORAGE: 0')
        m.key('f2'); m.type('usb install KITDEMO.BEX'); m.wait('Installed: KITDEMO')
        m.type('run KITDEMO'); m.wait('Hello from a native x86-64 assembly program!')
        m.wait('PROGRAM RETURN: KITDEMO')
        m.type('touch draft.txt'); m.type('edit draft.txt'); m.wait('UI APP: editor')
        m.key('tab'); m.type('alpha'); m.type('beta', enter=False)
        m.key('ctrl-f'); m.type('alpha'); m.key('esc'); m.key('shift-x'); m.key('ctrl-s')
        m.key('f2'); m.type('cat draft.txt'); m.wait('    Xalpha\n    beta')
        m.key('f6'); m.screenshot('editor-revision')
        print('PASS: App Kit build, USB install, native execution, auto-indent and editor search')
    finally:
        m.close()
