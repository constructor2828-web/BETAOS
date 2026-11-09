"""Launch/search, ordered file selection, preview scrolling and loss prevention."""
import time
from qemu_support import Machine

m=Machine('daily-workflow');x,y=500,380
def click(tx,ty):
    global x,y
    while (x,y)!=(tx,ty):
        dx=max(-60,min(60,tx-x));dy=max(-60,min(60,ty-y))
        m.hmp(f'mouse_move {dx} {dy}');x+=dx;y+=dy;time.sleep(.04)
    m.hmp('mouse_button 1');time.sleep(.08);m.hmp('mouse_button 0');time.sleep(.1)
def command(value,marker):
    offset=m.log.stat().st_size;m.type(value);m.wait(marker,start=offset)
try:
    m.key('f2')
    command('write alpha.txt original','Saved.')
    command('touch alpha.txt','File exists; contents preserved.')
    command('cat alpha.txt','\noriginal\n')
    command('write beta.txt destination','Saved.')
    command('cp alpha.txt beta.txt','Destination exists; nothing was overwritten.')
    command('cat beta.txt','\ndestination\n')
    command('mv alpha.txt beta.txt','Destination exists; nothing was overwritten.')
    command('cat alpha.txt','\noriginal\n')
    # Keep a terminal draft intact while searching/opening a built-in app.
    m.type('echo pending',enter=False);m.key('meta_l');m.type('editor',enter=False)
    m.screenshot('start-search');offset=m.log.stat().st_size;m.key('ret');m.wait('UI APP: editor',start=offset)
    m.key('ctrl-w');m.key('f2');m.key('ret');m.wait('CMD: echo pending')
    command('write test.asm ret','Saved.');command('asm test.asm custom','BUILD OK: custom')
    offset=m.log.stat().st_size;m.key('meta_l');m.type('custom',enter=False);m.key('ret');m.wait('PROGRAM RETURN: custom',start=offset)
    # Search has no-results handling; Escape preserves the active window.
    m.key('meta_l');m.type('no_such_app',enter=False);m.key('ret');m.key('esc')
    command('echo launcher-ok','\nlauncher-ok\n')
    # Name sorting must drive both keyboard selection and mouse hit testing.
    m.key('f3');click(240,150);m.wait('FILES: Sort: name / A-Z')
    m.key('home');m.key('delete');m.key('ret');m.wait('FILE DELETED: alpha.txt')
    m.key('ctrl-z');m.wait('FILES: File restored.')
    m.key('ctrl-f');m.type('beta.txt');m.key('down');m.key('ret');m.wait('UI APP: editor')
    # A terminal overwrite cannot silently destroy modified editor contents.
    m.key('ctrl-end');m.key('x');m.key('f2')
    command('write beta.txt overwrite','Save or discard editor changes before replacing this file.')
    command('rm beta.txt','Save or discard editor changes before deleting this file.')
    m.key('f6');m.key('ctrl-s');m.key('f2');command('cat beta.txt','\ndestinationx\n')
    command('write beta.txt refreshed','Saved.')
    m.key('f6');m.key('ctrl-s');m.key('f2');command('cat beta.txt','\nrefreshed\n')
    command('mv beta.txt renamed.txt','Done.')
    m.key('f6');m.key('ctrl-end');m.key('y');m.key('ctrl-s');m.key('f2')
    command('cat renamed.txt','\nrefreshedy\n')
    command('cat beta.txt','File not found.')
    # Delete/restore conflict preserves both the new file and the deleted snapshot.
    command('rm alpha.txt','FILE DELETED: alpha.txt')
    command('write alpha.txt replacement','Saved.')
    command('undelete','That name already exists.')
    command('cat alpha.txt','\nreplacement\n')
    command('mv alpha.txt replacement.txt','Done.')
    command('undelete','File restored.')
    command('cat alpha.txt','\noriginal\n')
    # Rename is possible even with all 16 slots occupied.
    command('write long.txt '+('0123456789'*55),'Saved.')
    m.type('edit long.txt');m.key('ctrl-home')
    for _ in range(25):m.key('ret')
    m.key('ctrl-s');m.key('f2')
    for i in range(10):m.type(f'touch filler{i}')
    command('touch overflow','Invalid filename or filesystem full.')
    command('mv alpha.txt final.txt','Done.')
    command('cat final.txt','\noriginal\n')
    # Sorted mouse selection and long-preview scrolling remain usable.
    m.key('f3');m.key('ctrl-f');m.type('long.txt');click(150,180);m.wait('UI FILE: long.txt')
    m.key('ctrl-pgdn');m.key('ctrl-pgdn');m.screenshot('files-daily-workflow')
    m.key('ctrl-pgup')
    m.key('ctrl-f');m.key('f2');command('echo focus-ok','\nfocus-ok\n')
    print('PASS: Start search/keyboard launch, preserved drafts, sorted files, undo delete, safe touch/cp/mv/write, editor synchronization and full-store rename')
finally:m.close()
