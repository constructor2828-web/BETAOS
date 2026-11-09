"""Exercise complete HTTP bodies across repeated 8 KiB RTL8139 ring boundaries."""
import os
from pathlib import Path
import re
import threading
import time
from qemu_support import Machine,ROOT

pipe=ROOT/'build/network-long-pipe'
for suffix in ('.in','.out'):
    path=Path(str(pipe)+suffix);path.unlink(missing_ok=True);os.mkfifo(path)
input_fd=os.open(str(pipe)+'.in',os.O_RDWR)
output_fd=os.open(str(pipe)+'.out',os.O_RDWR)

def server():
    pending=bytearray()
    for index in range(3):
        while b'\r\n\r\n' not in pending:pending.extend(os.read(output_fd,4096))
        request,_,rest=pending.partition(b'\r\n\r\n');pending[:]=rest
        body=b'<h1>Large response</h1><p>'+bytes([65+index])*12000+b'</p><p>STREAM'+str(index).encode()+b'_END</p>'
        response=b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body
        remaining=memoryview(response)
        while remaining:remaining=remaining[os.write(input_fd,remaining):]
threading.Thread(target=server,daemon=True).start()
m=Machine('network-long',network=['-netdev',f'user,id=n0,guestfwd=tcp:10.0.2.100:80-pipe:{pipe}','-device','rtl8139,netdev=n0'])
try:
    durations=[]
    for index in range(3):
        m.key('f1');offset=m.log.stat().st_size;before=time.monotonic()
        m.type(f'http://10.0.2.100/stream{index}')
        m.wait(f'STREAM{index}_END',start=offset,timeout=15)
        m.wait('HTTP OK: 10.0.2.100',start=offset,timeout=15)
        durations.append(round(time.monotonic()-before,3))
    m.key('f2');m.type('netstats');m.wait('NETSTATS packets=')
    match=re.search(r'NETSTATS packets=(\d+) crossings=(\d+) bad_checksums=(\d+) resets=(\d+)',m.log.read_text())
    assert match
    packets,crossings,bad,resets=map(int,match.groups())
    assert crossings>=3,(packets,crossings,bad,resets)
    assert bad==0 and resets==0,(packets,crossings,bad,resets)
    print(f'PASS: three complete 12 KiB HTTP bodies; {crossings} ring crossings, no checksum corruption or RX resets; request times={durations}')
finally:
    m.close();os.close(input_fd);os.close(output_fd)
    for suffix in ('.in','.out'):Path(str(pipe)+suffix).unlink(missing_ok=True)
