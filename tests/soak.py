"""Repeat normal desktop activity for a bounded duration without touching user disks."""
import argparse
import json
import re
import time
from qemu_support import Machine,ROOT

parser=argparse.ArgumentParser()
parser.add_argument('--seconds',type=int,default=300)
args=parser.parse_args()
m=Machine('soak')
try:
    m.key('f6');m.key('ctrl-s');m.key('ctrl-r');m.wait('PROGRAM RETURN: scratch')
    m.key('f2');m.type('install fib');m.wait('Installed: fib')
    begin=time.monotonic();end=begin+args.seconds;iteration=0
    while time.monotonic()<end:
        iteration+=1
        m.key('f2');m.type(f'write soak tick{iteration}')
        offset=m.log.stat().st_size;m.type('cat soak');m.wait(f'\ntick{iteration}\n',start=offset)
        m.key('f3');m.key('pgdn');m.key('pgup');m.key('alt-tab');m.key('f2')
        m.type('edit soak');m.key('x');m.key('ctrl-z');m.key('ctrl-s');m.key('ctrl-w')
        m.key('f5');time.sleep(.15);m.key('spc');m.key('f2')
        offset=m.log.stat().st_size;m.type('run hello');m.wait('PROGRAM RETURN: hello',start=offset)
        if iteration%4==0:
            offset=m.log.stat().st_size;m.type('run fib');m.wait('PROGRAM RETURN: fib',start=offset)
            m.type('apps');m.key('pgdn');m.key('pgup');m.key('ctrl-w')
        if iteration%7==0:
            m.key('f1');m.type('http://invalid..host/');m.key('f2');m.type('echo background-alive');m.wait('CMD: echo background-alive')
        log=m.log.read_text(errors='replace')
        assert 'PANIC' not in log and 'FATAL' not in log
        assert m.command('query-status')['running']
    m.key('f2');m.type('perf');m.wait('RENDER frames=');m.screenshot('soak-final')
    elapsed=time.monotonic()-begin;log=m.log.read_text()
    result={'duration_seconds':round(elapsed,2),'iterations':iteration,'commands':log.count('CMD:'),
            'native_program_returns':log.count('PROGRAM RETURN:'),'panic_count':log.count('PANIC'),'fatal_count':log.count('FATAL')}
    (ROOT/'qa/soak-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: sustained desktop activity '+json.dumps(result))
finally:m.close()
