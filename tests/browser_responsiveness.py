"""A slow HTTP response must not block other apps, animation, or cancellation."""
from pathlib import Path
import os
import threading
import time
from qemu_support import Machine,ROOT

pipe=ROOT/'build/slow-http'
for suffix in ('.in','.out'):
    path=Path(str(pipe)+suffix);path.unlink(missing_ok=True);os.mkfifo(path)
read_fd=os.open(str(pipe)+'.out',os.O_RDWR)
write_fd=os.open(str(pipe)+'.in',os.O_RDWR)
received=[threading.Event(),threading.Event()]
release=[threading.Event(),threading.Event()]

def server():
    pending=bytearray()
    try:
        for index in range(2):
            while b'\r\n\r\n' not in pending:pending.extend(os.read(read_fd,4096))
            _,_,rest=pending.partition(b'\r\n\r\n');pending[:]=rest
            body=f'<p>SLOW{index}_DONE</p>'.encode()
            os.write(write_fd,b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n')
            received[index].set();release[index].wait(15)
            os.write(write_fd,body)
    except OSError:
        pass
threading.Thread(target=server,daemon=True).start()
m=Machine('slow-http',network=['-netdev',f'user,id=n0,guestfwd=tcp:10.0.2.100:80-pipe:{pipe}','-device','rtl8139,netdev=n0'])
try:
    m.key('f1');m.type('http://10.0.2.100/slow0');assert received[0].wait(5)
    before=time.monotonic();m.key('f2');m.type('echo while-loading');m.wait('CMD: echo while-loading',timeout=2)
    assert time.monotonic()-before<2,'Slow network response blocked terminal input'
    m.key('f5');time.sleep(.15);m.screenshot('gears-during-download')
    release[0].set();m.wait('SLOW0_DONE');m.wait('HTTP OK:')
    m.key('f1');m.type('http://10.0.2.100/slow1');assert received[1].wait(5)
    m.key('esc');m.wait('HTTP CANCELLED');m.key('f2');m.type('echo after-cancel');m.wait('CMD: echo after-cancel',timeout=2)
    release[1].set()
    print('PASS: slow HTTP stays in background, terminal and animation remain usable, Escape cancels promptly')
finally:
    for event in release:event.set()
    m.close();os.close(read_fd);os.close(write_fd)
    for suffix in ('.in','.out'):Path(str(pipe)+suffix).unlink(missing_ok=True)
