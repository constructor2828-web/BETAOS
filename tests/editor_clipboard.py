"""Exercise editor selection, clipboard, undo and bounded paste through PS/2."""
import time
from qemu_support import Machine

m = Machine('editor-clipboard')

def contents(name, expected):
    m.key('f2')
    offset = m.log.stat().st_size
    m.type('cat ' + name)
    m.wait('\n' + expected + '\n', start=offset)

try:
    m.key('f2'); m.type('write source.txt alpha'); m.type('edit source.txt')
    m.key('ctrl-a'); m.key('ctrl-c'); m.key('ctrl-x'); m.key('ctrl-z'); m.key('ctrl-s')
    contents('source.txt', 'alpha')
    m.type('write target.txt beta'); m.type('edit target.txt')
    m.key('ctrl-a'); m.key('ctrl-v'); m.key('ctrl-z'); m.key('ctrl-s')
    contents('target.txt', 'beta')
    m.key('f6'); m.key('ctrl-y'); m.key('ctrl-s')
    contents('target.txt', 'alpha')
    m.key('f6'); m.key('ctrl-a'); m.key('x'); m.key('ctrl-z'); m.key('ctrl-s')
    contents('target.txt', 'alpha')
    m.key('f6'); m.key('ctrl-home'); m.key('ctrl-v'); m.key('ctrl-s')
    contents('target.txt', 'alphaalpha')
    m.key('f6'); m.key('ctrl-z')
    for _ in range(10):
        m.key('ctrl-a'); m.key('ctrl-c'); m.key('ctrl-end'); m.key('ctrl-v')
    m.key('ctrl-s')
    contents('target.txt', 'alpha' * 1024)
    m.key('f6'); m.key('ctrl-a'); m.key('ctrl-c'); m.key('ctrl-end'); m.key('ctrl-v')
    m.key('ctrl-s')
    contents('target.txt', 'alpha' * 1024)
    # A rejected paste must not consume an undo checkpoint.
    m.key('f6'); m.key('ctrl-z'); m.key('ctrl-s')
    contents('target.txt', 'alpha' * 512)
    offset = m.log.stat().st_size
    m.key('f6'); m.wait('UI APP: editor', start=offset); m.key('ctrl-a')
    time.sleep(.2)
    m.screenshot('editor-selection')
    print('PASS: select all, cross-file copy, cut, replacement, paste, undo/redo and atomic size-limit rejection')
finally:
    m.close()
