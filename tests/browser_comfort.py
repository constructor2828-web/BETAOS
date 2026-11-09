"""Real page layout, hidden fields, binary PNG, editing, history and wheel input."""
import os,select,struct,threading,time,zlib
from pathlib import Path
from urllib.parse import urlsplit,parse_qs
from qemu_support import Machine,ROOT

def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
pixels=b''.join(b'\0'+b''.join(bytes((34,103,202,255)) if x<64 else bytes((32,167,112,255)) for x in range(128)) for y in range(40))
PNG=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',128,40,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
HTML=b'''<!doctype html><html><head><title>Search layout lab</title><link rel=stylesheet href=/assets/main.css></head><body><center>
<h1>Search layout lab</h1><img src=/logo.png width=128 height=40 alt="Search logo"><form action=/search><table><tr><td>
<input name=token type=hidden value=fixed><input name=q size=40 placeholder="Search this fixture"><br>
<input name=btnG type=submit value=Search><input name=btnI type=submit value=Lucky></td></tr></table></form>
<p id=priority>Styles, images and forms</p><div class=row><div class=tile>Left</div><div class=tile>Right</div></div>
''' + b'<p>Scrollable page content</p>'*24+b'</center></body></html>'
CSS=b'''@import url('../theme.css'); input{display:inline-block}input[type=hidden]{display:block!important}
body{margin:0;padding:22px;text-align:center;background:#edf3fb}h1{font-size:24px}.row{display:flex;gap:16px;max-width:600px;margin:0 auto}.tile{flex:1;background:var(--brand);padding:16px;color:white}#priority{color:#ff0000!important}'''
THEME=b':root{--brand:#2267ca}input{border-radius:8px}input[type=submit]{background:#eef0f4} @media(max-width:500px){body{background:#ffeeee}}'
pipe=ROOT/'build/comfort-pipe'
for suffix in ('.in','.out'):
 p=Path(str(pipe)+suffix);p.unlink(missing_ok=True);os.mkfifo(p)
read_fd=os.open(str(pipe)+'.out',os.O_RDWR|os.O_NONBLOCK);write_fd=os.open(str(pipe)+'.in',os.O_RDWR)
stop=threading.Event();requests=[]
def server():
 pending=bytearray()
 while not stop.is_set():
  if not select.select([read_fd],[],[],.05)[0]:continue
  try:pending.extend(os.read(read_fd,65536))
  except BlockingIOError:continue
  while b'\r\n\r\n' in pending:
   request,_,rest=pending.partition(b'\r\n\r\n');pending[:]=rest
   path=request.split(b' ')[1].decode();requests.append(path);typ=b'text/html'
   if path=='/assets/main.css':body,typ=CSS,b'text/css'
   elif path=='/theme.css':body,typ=THEME,b'text/css'
   elif path=='/logo.png':body,typ=PNG,b'image/png'
   elif path.startswith('/search?'):body=b'<h1>COMFORT_SEARCH_OK</h1><p>Actual GET result.</p><a href="/details">Open result</a>'
   elif path=='/details':body=b'<h1>COMFORT_DETAILS_OK</h1>'
   else:body=HTML
   wire=b'HTTP/1.1 200 OK\r\nContent-Type: '+typ+b'\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body
   while wire:wire=wire[os.write(write_fd,wire):]
thread=threading.Thread(target=server,daemon=True);thread.start()
m=Machine('browser-comfort',extra=['-cpu','max'],network=['-netdev',f'user,id=n0,guestfwd=tcp:10.0.2.100:80-pipe:{pipe}','-device','rtl8139,netdev=n0'])
try:
 m.key('f1');m.key('ctrl-l');m.type('http://10.0.2.100/')
 m.wait('WEB IMAGE: decoded');m.wait('HTTP OK:')
 deadline=time.monotonic()+10
 while '/theme.css' not in requests and time.monotonic()<deadline:time.sleep(.01)
 assert '/theme.css' in requests,requests
 time.sleep(.4);picture=m.screenshot('browser08-styled')
 from PIL import Image
 im=Image.open(picture).convert('RGB')
 assert sum(p==(34,103,202) for p in list(zip(*[iter(im.tobytes())]*3)))>=128*40//3,'PNG pixels not painted'
 assert sum(p==(32,167,112) for p in list(zip(*[iter(im.tobytes())]*3)))>=128*40//3,'PNG alpha/color corruption'
 before=im.crop((49,178,962,620)).tobytes();m.command('input-send-event',{'events':[{'type':'btn','data':{'button':'wheel-down','down':True}},{'type':'btn','data':{'button':'wheel-down','down':False}}]*10});time.sleep(.2);scrolled=Image.open(m.screenshot('browser08-scrolled')).crop((49,178,962,620)).tobytes();assert scrolled!=before,'Wheel did not scroll'
 m.key('pgup');m.key('tab');m.type('abxd',enter=False);m.key('left');m.key('left');m.key('delete');m.key('c');m.key('ret')
 m.wait('COMFORT_SEARCH_OK')
 search=[url for url in requests if url.startswith('/search?')][-1];query=parse_qs(urlsplit(search).query)
 assert query=={'token':['fixed'],'q':['abcd']},query
 m.key('alt-left');time.sleep(.4);m.key('alt-right');time.sleep(.4)
 assert sum(path.startswith('/search?') for path in requests)>=2,requests
 m.key('ctrl-l');m.type('http://10.0.2.100/details');m.wait('COMFORT_DETAILS_OK')
 m.key('ctrl-l');m.key('ctrl-a');m.type('http://10.0.2.100/');time.sleep(.4)
 assert requests.count('/')>=3,requests
 long_path='a'*110
 m.key('ctrl-l');m.type('http://10.0.2.100/'+long_path,enter=False);m.key('home');time.sleep(.1);m.screenshot('browser08-address-edit');m.key('end');m.key('left');m.key('left');m.key('left');m.key('delete');m.type('b');time.sleep(.2)
 assert '/'+('a'*107)+'b'+('a'*2) in requests,requests
 assert 'FAULT' not in m.log.read_text() and 'ASSERTION' not in m.log.read_text()
 print('PASS: real PNG pixels, stylesheet imports, hidden input regression, form editing/GET, back/forward, address bar and PS/2 wheel')
finally:
 m.close();stop.set();thread.join(timeout=2);os.close(read_fd);os.close(write_fd)
 for suffix in ('.in','.out'):Path(str(pipe)+suffix).unlink(missing_ok=True)
