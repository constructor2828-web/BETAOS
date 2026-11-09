"""Intentional faults from disposable native ASM programs must report correct frames."""
from qemu_support import Machine

cases=[('ud2',['db 15,11'],'PANIC: vector=6 error=0',None),
       ('page-fault',['mov rax, 0x00007fffffff0000','db 72,139,0'],'PANIC: vector=14','cr2=0x00007fffffff0000')]
for name,source,marker,address in cases:
    m=Machine('fault-'+name)
    try:
        m.key('f3');m.key('ctrl-n');m.type(name+'.asm');m.key('ret')
        for line in source:m.type(line)
        m.key('ctrl-r');m.wait(marker);m.wait('PANIC HALTED')
        if address:m.wait(address)
        print(f'PASS: intentional {name} reports the correct vector, CPU error frame and fault address')
    finally:m.close()
