"""Socket-free QEMU harness for interaction, profiling and fault regression tests."""
import json
import os
from pathlib import Path
import select
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


class Machine:
    def __init__(self, name='interaction', iso=None, extra=(), disk_only=False, network=None):
        self.log = ROOT / 'build' / f'{name}.log'
        self.log.write_text('')
        self.buffer = bytearray()
        self.process = subprocess.Popen([
            'qemu-system-x86_64', '-machine', 'q35', '-m', '512',
            '-bios', os.environ.get('OVMF', '/usr/share/OVMF/x64/OVMF.4m.fd'),
            *([] if disk_only else ['-cdrom', str(iso or ROOT / 'build/BETAOS64.iso')]),
            '-vga', 'std', *(network if network is not None else ['-nic', 'user,model=rtl8139']),
            '-display', 'none', '-serial', f'file:{self.log}',
            '-monitor', 'none', '-qmp', 'stdio', '-no-reboot', *extra,
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self._read()
        self.command('qmp_capabilities')
        self.wait('DESKTOP READY')
        self.wait('DHCP configured')

    def _read(self, timeout=15):
        until = time.monotonic() + timeout
        while b'\n' not in self.buffer:
            if time.monotonic() > until:
                raise AssertionError('QMP timeout')
            if select.select([self.process.stdout], [], [], .05)[0]:
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise AssertionError(self.process.stderr.read().decode() + self.log.read_text())
                self.buffer.extend(chunk)
        line, _, rest = self.buffer.partition(b'\n')
        self.buffer[:] = rest
        return json.loads(line)

    def command(self, command, args=None):
        self.process.stdin.write((json.dumps({'execute': command, 'arguments': args or {}}) + '\n').encode())
        self.process.stdin.flush()
        while True:
            result = self._read()
            if 'return' in result:
                return result['return']
            if 'error' in result:
                raise AssertionError(result)

    def hmp(self, command):
        return self.command('human-monitor-command', {'command-line': command})

    def key(self, name, delay=.04):
        self.hmp(f'sendkey {name} 20')
        time.sleep(delay)

    def type(self, value, enter=True):
        special = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', ':': 'shift-semicolon', '_': 'shift-minus', ',': 'comma', ';': 'semicolon', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right', '+': 'shift-equal'}
        for char in value:
            self.key('shift-'+char.lower() if char.isupper() else special.get(char, char))
        if enter:
            self.key('ret')

    def wait(self, marker, start=0, timeout=30):
        until = time.monotonic() + timeout
        while marker.encode() not in self.log.read_bytes()[start:]:
            if self.process.poll() is not None or time.monotonic() > until:
                raise AssertionError(f'Missing {marker!r}\n{self.log.read_text(errors="replace")[-2500:]}')
            time.sleep(.005)

    def screenshot(self, name):
        path = ROOT / 'build' / f'{name}.ppm'
        self.command('screendump', {'filename': str(path)})
        return path

    def cpu_seconds(self):
        stat = Path(f'/proc/{self.process.pid}/stat').read_text().rsplit(')', 1)[1].split()
        return (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK')

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()
