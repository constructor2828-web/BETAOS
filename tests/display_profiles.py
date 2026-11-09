"""Boot alternate GOP modes and exercise constrained-layout bounds."""
import re
import subprocess
import sys
import time
from qemu_support import Machine,ROOT

profiles=sys.argv[1:] or ['800x600','1280x800','1920x1080','800x601']
for profile in profiles:
    subprocess.run([sys.executable,str(ROOT/'tools/profile_iso.py'),profile],check=True,stdout=subprocess.DEVNULL)
    machine=Machine('display-'+profile,ROOT/'build'/('profile-'+profile)/'BETAOS.iso')
    try:
        log=machine.log.read_text()
        match=re.search(r'GFX: (\d+)x(\d+)',log)
        assert match,log
        width,height=map(int,match.groups())
        assert width>=800 and height>=600 and width<=4096 and height<=2160
        if profile!='800x601':
            assert (width,height)==tuple(map(int,profile.split('x'))),(profile,width,height)
        machine.key('f2');machine.type('echo profile-ready');machine.wait('CMD: echo profile-ready')
        if profile=='800x600':
            for index in range(15):
                machine.type(f'touch f{index}')
            machine.key('f3');machine.key('pgdn');machine.screenshot('files-800-scrolled')
            machine.key('pgup');machine.key('ctrl-n');machine.type('overflow')
            machine.wait('FILE CREATE FAILED: full')
            machine.key('esc')
        for key in ['f3','f4','f5','f6']:
            machine.key(key);time.sleep(.08)
            if key=='f5':machine.key('spc')
            shot=machine.screenshot(profile+'-'+key)
            _,dimensions,_,pixels=shot.read_bytes().split(b'\n',3)
            assert tuple(map(int,dimensions.split()))==(width,height)
            assert len(pixels)==width*height*3
        machine.key('f2');machine.type('perf');machine.wait('RENDER frames=')
        assert 'FATAL' not in machine.log.read_text()
        print(f'PASS: {profile} -> {width}x{height}, apps render inside framebuffer, input remains responsive')
    finally:
        machine.close()
