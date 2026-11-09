"""Exercise real QuickJS, DOM/CSS, asset requests, fetch, timers and forms in QEMU."""
import os
from pathlib import Path
import select
import threading
import time
from qemu_support import Machine, ROOT

HTML = b'''<!doctype html><html><head><title>Native Web Lab</title>
<link rel="stylesheet" href="/theme.css"></head><body>
<h1>BETA Browser</h1><p>HTML + CSS + JavaScript running inside BETAOS</p>
<div id="card"><h2 id="result">Loading JavaScript...</h2>
<button id="button" type="button">Run native JavaScript</button></div>
<form action="/search"><input name="q" placeholder="Search"><button>Search</button></form>
<script defer src="/deferred.js"></script><script src="/dependency.js"></script>
<script>if(window.deferred) throw Error('defer ran too soon');if(window.dependency !== 42) throw Error('external script order');console.log('WEB_ORDER_OK');</script>
<script src="/app.js"></script></body></html>'''
CSS = b'''body {background:#edf2fb;color:#162c50;font-size:16px;padding:18px}
h1 {color:#2463de;font-size:28px} #card {background:#ffffff;padding:18px;
border:1px solid #a6bce0;border-radius:12px;margin:12px} button {background:#2463de;color:white;padding:10px}'''
JS = b'''
if(location.href !== 'http://10.0.2.100/') throw Error('location getter');
const result = document.getElementById('result');
result.textContent = 'Real JavaScript, real DOM';
result.style.color = '#165bc5';
result.classList.add('ready');
if (!result.classList.contains('ready')) throw Error('classList');
const value = [1,2,3].map(x => x * 2).reduce((a,b)=>a+b,0);
if (value !== 12 || (2n ** 64n).toString() !== '18446744073709551616') throw Error('ECMAScript');
if(document.readyState !== 'loading') throw Error('readyState during script');
window.onload = () => { if(document.readyState !== 'complete') throw Error('readyState at load'); console.log('WEB_ONLOAD_OK'); };
console.log('WEB_SCRIPT_OK');
document.getElementById('button').addEventListener('click', e => {
 e.preventDefault(); result.textContent = 'Button event works'; console.log('WEB_CLICK_OK');
});
document.addEventListener('DOMContentLoaded', () => console.log('WEB_LOADED_OK'));
setTimeout(() => { console.log('WEB_TIMER_OK'); }, 100);
fetch('/data.json').then(r => r.json()).then(data => {
 if(data.answer !== 42) throw Error('JSON'); console.log('WEB_FETCH_OK');
});
'''
pipe = ROOT/'build/web-native-pipe'
for suffix in ('.in', '.out'):
    path = Path(str(pipe)+suffix); path.unlink(missing_ok=True); os.mkfifo(path)
read_fd = os.open(str(pipe)+'.out', os.O_RDWR|os.O_NONBLOCK)
write_fd = os.open(str(pipe)+'.in', os.O_RDWR)
stop = threading.Event()
requests = []
def server():
    pending = bytearray()
    while not stop.is_set():
        if not select.select([read_fd], [], [], .05)[0]: continue
        try: data = os.read(read_fd, 65536)
        except BlockingIOError: continue
        pending.extend(data)
        while b'\r\n\r\n' in pending:
            request, _, rest = pending.partition(b'\r\n\r\n'); pending[:] = rest
            path = request.split(b' ')[1].decode(); requests.append(path)
            content_type = b'text/html'
            if path == '/deferred.js': body, content_type = b"if(!window.dependency)throw Error('defer dependency');window.deferred=true;console.log('WEB_DEFER_OK');", b'text/javascript'
            elif path == '/dependency.js': body, content_type = b'window.dependency=42;', b'text/javascript'
            elif path == '/theme.css': body, content_type = CSS, b'text/css'
            elif path == '/app.js': body, content_type = JS, b'text/javascript'
            elif path == '/data.json': body, content_type = b'{"answer":42}', b'application/json'
            elif path.startswith('/search?'): body = b'<h1>WEB_FORM_OK</h1>'
            elif path == '/location': body = b"<script>location.href='/location-done'</script>"
            elif path == '/location-done': body = b'<p>WEB_LOCATION_OK</p>'
            elif path == '/loop': body = b'<script>while(true){}</script><p>WEB_TIMEOUT_RECOVERED</p>'
            else: body = HTML
            wire = b'HTTP/1.1 200 OK\r\nContent-Type: '+content_type+b'\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body
            while wire: wire = wire[os.write(write_fd, wire):]
thread = threading.Thread(target=server, daemon=True); thread.start()
m = Machine('web-native', extra=['-cpu','max'], network=['-netdev',f'user,id=n0,guestfwd=tcp:10.0.2.100:80-pipe:{pipe}','-device','rtl8139,netdev=n0'])
try:
    m.key('f1'); m.type('http://10.0.2.100/')
    for marker in ('WEB_ORDER_OK','WEB_DEFER_OK','WEB_SCRIPT_OK','WEB_TIMER_OK','WEB_FETCH_OK','WEB_LOADED_OK','WEB_ONLOAD_OK'): m.wait(marker)
    assert all(path in requests for path in ('/','/theme.css','/app.js','/data.json')), requests
    time.sleep(.3); m.screenshot('browser-web-native')
    m.key('tab'); m.key('ret'); m.wait('WEB_CLICK_OK')
    m.key('tab'); m.type('native search'); m.wait('WEB_FORM_OK')
    assert '/search?q=native%20search' in requests, requests
    m.key('ctrl-l'); m.type('http://10.0.2.100/location'); m.wait('WEB_LOCATION_OK')
    assert '/location-done' in requests, requests
    m.key('ctrl-l'); m.type('http://10.0.2.100/loop')
    m.wait('interrupted'); m.wait('WEB_TIMEOUT_RECOVERED')
    m.key('f2'); m.type('echo after-js-timeout'); m.wait('CMD: echo after-js-timeout')
    assert 'FAULT' not in m.log.read_text() and 'ASSERTION' not in m.log.read_text()
    print('PASS: native ECMAScript, DOM changes, external CSS/JS, timers, fetch JSON, keyboard events, form GET and script timeout recovery')
finally:
    m.close(); stop.set(); thread.join(timeout=2)
    os.close(read_fd); os.close(write_fd)
    for suffix in ('.in','.out'): Path(str(pipe)+suffix).unlink(missing_ok=True)
