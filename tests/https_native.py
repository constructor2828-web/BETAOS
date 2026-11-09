"""Native TLS 1.2, verified X.509, chunked HTTP and wrong-host rejection.
The test CA is linked into a separate EFI/ISO; it is never trusted by the release.
"""
import os,select,ssl,subprocess,threading,time
from pathlib import Path
from qemu_support import Machine,ROOT
import sys
sys.path.insert(0,str(ROOT/'tools'))
from tls_roots import generate
D=ROOT/'build/https-fixture';D.mkdir(exist_ok=True)
def run(*args):subprocess.run(args,cwd=ROOT,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
if not (D/'ca.pem').exists():
 run('openssl','req','-x509','-newkey','rsa:2048','-nodes','-sha256','-days','30','-keyout',str(D/'ca.key'),'-out',str(D/'ca.pem'),'-subj','/CN=BETAOS TEST CA ONLY')
for label,host in [('valid','10.0.2.100'),('wrong','wrong.example')]:
 (D/f'{label}.ext').write_text(f'subjectAltName=DNS:{host}\nextendedKeyUsage=serverAuth\n')
 if not (D/f'{label}.pem').exists():
  run('openssl','req','-newkey','rsa:2048','-nodes','-sha256','-keyout',str(D/f'{label}.key'),'-out',str(D/f'{label}.csr'),'-subj',f'/CN={host}')
  run('openssl','x509','-req','-in',str(D/f'{label}.csr'),'-CA',str(D/'ca.pem'),'-CAkey',str(D/'ca.key'),'-CAcreateserial','-days','7','-extfile',str(D/f'{label}.ext'),'-out',str(D/f'{label}.pem'))
(D/'tls_roots.h').write_text(generate((D/'ca.pem').read_text()))
flags=['-m64','-mcmodel=large','-mno-red-zone','-mgeneral-regs-only','-std=gnu11','-ffreestanding','-fno-pie','-fno-stack-protector','-fno-builtin','-fno-asynchronous-unwind-tables','-fcf-protection=none','-O2']
run('gcc',f'-I{D}','-Ivendor/bearssl/inc','-Ivendor/bearssl/src',*flags,'-c','desktop/tls.c','-o',str(D/'tls.o'))
run('objcopy','--remove-section','.comment','--remove-section','.note.gnu.property',str(D/'tls.o'))
objects=subprocess.check_output(['make','-s','print-kernel-link'],cwd=ROOT,text=True).split()
objects=[str(D/'tls.o') if name=='build/64/tls.o' else name for name in objects]
run('ld','-mi386pep','--subsystem','10','--image-base','0x100000','-e','efi_main',*objects,'-o',str(D/'BOOTX64.EFI'))
run('objcopy','--remove-section','.comment','--remove-section','.eh_frame','--remove-section','.note.gnu.property',str(D/'BOOTX64.EFI'))
(D/'iso/boot').mkdir(parents=True,exist_ok=True)
run('python3','tools/make_esp.py',str(D/'BOOTX64.EFI'),str(D/'iso/boot/efi.img'))
iso=D/'https-test.iso'
run('genisoimage','-quiet','-R','-J','-V','BETA-TLS-TEST','-eltorito-platform','efi','-b','boot/efi.img','-no-emul-boot','-o',str(iso),str(D/'iso'))

def scenario(label, release=False, future=False):
 pipe=D/f'pipe-{label}-{release}'
 for suffix in ('.in','.out'):
  p=Path(str(pipe)+suffix);p.unlink(missing_ok=True);os.mkfifo(p)
 incoming=os.open(str(pipe)+'.out',os.O_RDWR|os.O_NONBLOCK)
 outgoing=os.open(str(pipe)+'.in',os.O_RDWR)
 stop=threading.Event();received=[];errors=[]
 def server():
  context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.maximum_version=ssl.TLSVersion.TLSv1_2
  context.load_cert_chain(str(D/f'{label}.pem'),str(D/f'{label}.key'))
  raw_in=ssl.MemoryBIO();raw_out=ssl.MemoryBIO();peer=context.wrap_bio(raw_in,raw_out,server_side=True)
  def flush():
   data=raw_out.read()
   if data:os.write(outgoing,data)
  try:
   handshake=False;request=bytearray();answered=False
   while not stop.is_set():
    try:
     if not handshake:peer.do_handshake();handshake=True
     elif not answered:
      request.extend(peer.read(16384))
      if b'\r\n\r\n' in request:
       received.append(bytes(request))
       body=b'<html><body><h1>TLS_NATIVE_OK</h1><p>Verified certificate and real encrypted records.</p></body></html>'
       payload=b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: text/html\r\n\r\n'
       for piece in (body[:27],body[27:]):payload+=hex(len(piece))[2:].encode()+b'\r\n'+piece+b'\r\n'
       payload+=b'0\r\nX-Fixture: yes\r\n\r\n';peer.write(payload);answered=True
     else:
      try:peer.unwrap()
      except ssl.SSLWantReadError:pass
    except (ssl.SSLWantReadError,ssl.SSLWantWriteError):pass
    flush()
    if select.select([incoming],[],[],.02)[0]:
     try:data=os.read(incoming,65536)
     except BlockingIOError:continue
     if data:raw_in.write(data)
  except (OSError,ssl.SSLError) as error:errors.append(str(error))
 thread=threading.Thread(target=server,daemon=True);thread.start()
 m=Machine(f'https-{label}-{release}',iso=None if release else iso,extra=['-cpu','max']+(['-rtc','base=2030-01-01T00:00:00'] if future else []),network=['-netdev',f'user,id=n0,guestfwd=tcp:10.0.2.100:443-pipe:{pipe}','-device','rtl8139,netdev=n0'])
 try:
  m.key('f1');m.type('https://10.0.2.100/')
  if label=='valid' and not release and not future:
   m.wait('TLS VERIFIED:',timeout=45);m.wait('TLS_NATIVE_OK',timeout=30);m.wait('HTTP OK:')
   assert received and received[0].startswith(b'GET / HTTP/1.1')
   m.screenshot('https-native');print('PASS: real native TLS 1.2, trusted CA/hostname, encrypted HTTP and chunked body',flush=True)
  else:
   m.wait('TLS validation failed:',timeout=45)
   assert 'TLS_NATIVE_OK' not in m.log.read_text() and not received
   print('PASS: '+('certificate date rejected' if future else 'release rejects test CA' if release else 'wrong hostname rejected'),flush=True)
 finally:
  m.close();stop.set();thread.join(timeout=2);os.close(incoming);os.close(outgoing)
  for suffix in ('.in','.out'):Path(str(pipe)+suffix).unlink(missing_ok=True)
scenario('valid');scenario('wrong');scenario('valid',release=True);scenario('valid',future=True)
