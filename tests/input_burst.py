"""Fast native PS/2 typing must remain accurate and overlong lines must not execute."""
import time
from qemu_support import Machine

m=Machine('input-burst')
try:
    m.key('f2')
    text='echo '+''.join(chr(97+i%26) for i in range(240))
    for start in range(0,len(text),8):
        events=[]
        for char in text[start:start+8]:
            qcode='spc' if char==' ' else char
            for down in (True,False):events.append({'type':'key','data':{'down':down,'key':{'type':'qcode','data':qcode}}})
        m.command('input-send-event',{'events':events});time.sleep(.06)
    m.key('ret');m.wait('CMD: '+text)
    # More than the 639-byte line limit must fail, including after app switching.
    text='echo '+('a'*650)
    for start in range(0,len(text),8):
        events=[]
        for char in text[start:start+8]:
            qcode='spc' if char==' ' else char
            for down in (True,False):events.append({'type':'key','data':{'down':down,'key':{'type':'qcode','data':qcode}}})
        m.command('input-send-event',{'events':events});time.sleep(.06)
    m.key('f3');m.key('f2');offset=m.log.stat().st_size;m.key('ret')
    m.wait('Input too long: command was not executed.',start=offset)
    assert 'CMD: echo aaaaaa' not in m.log.read_bytes()[offset:].decode(errors='replace')
    m.type('echo recovered');m.wait('CMD: echo recovered')
    print('PASS: fast PS/2 typing is accurate; oversized lines survive app switching and are rejected without execution')
finally:m.close()
