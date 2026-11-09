"""Ensure cursor-only presentation restores scene pixels without trails."""
import re
import time
from qemu_support import Machine

m=Machine('render-correctness')
try:
    before=m.screenshot('cursor-before').read_bytes().split(b'\n',3)[3]
    x,y=500,380
    for tx,ty in [(510,390),(430,310),(350,390),(420,450),(500,380),(520,400)]:
        while (x,y)!=(tx,ty):
            dx=max(-40,min(40,tx-x));dy=max(-40,min(40,ty-y));m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.01)
    time.sleep(.05)
    after=m.screenshot('cursor-after').read_bytes().split(b'\n',3)[3]
    # Exclude the old/new pointer bounding boxes. The clock sits below this region.
    for row in range(0,680):
        for col in range(1024):
            if (500<=col<510 and 380<=row<395) or (520<=col<530 and 400<=row<415):continue
            index=(row*1024+col)*3
            assert before[index:index+3]==after[index:index+3],(col,row)
    m.key('f2');m.type('perf');m.wait('RENDER frames=')
    first=list(re.finditer(r'RENDER frames=(\d+) cursor=(\d+).*?video_bytes=(\d+)',m.log.read_text()))[-1]
    for step in range(30):m.hmp(f'mouse_move {1 if step%2 else -1} 0');time.sleep(.005)
    offset=m.log.stat().st_size;m.type('perf');m.wait('RENDER frames=',start=offset)
    second=list(re.finditer(r'RENDER frames=(\d+) cursor=(\d+).*?video_bytes=(\d+)',m.log.read_text()))[-1]
    f1,c1,b1=map(int,first.groups());f2,c2,b2=map(int,second.groups())
    assert c2>c1 and f2-f1<15,(f1,f2,c1,c2)
    assert b2-b1<1024*1024,(b1,b2)
    print(f'PASS: cursor leaves no trails; 30 pointer events added {f2-f1} UI frames and {b2-b1} video bytes including the diagnostic command')
finally:m.close()
