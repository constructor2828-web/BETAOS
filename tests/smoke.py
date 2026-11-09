"""Exercise the actual booted kernel over its COM1 console."""
import pathlib
import select
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
process = subprocess.Popen([
    "qemu-system-i386", "-drive",
    f"file={ROOT / 'build/betaos.img'},format=raw,if=floppy",
    "-display", "none", "-serial", "stdio", "-monitor", "none",
    "-no-reboot", "-no-shutdown",
], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
transcript = bytearray()


def receive(marker=b"beta> ", timeout=10):
    result = bytearray()
    deadline = time.monotonic() + timeout
    while marker not in result:
        if time.monotonic() > deadline:
            raise AssertionError(f"Timed out waiting for {marker!r}: {bytes(result)!r}")
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            import os
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                raise AssertionError("QEMU exited: " + process.stderr.read().decode())
            result.extend(chunk)
            transcript.extend(chunk)
    return result.decode("ascii", errors="replace")


def run(command, expected):
    process.stdin.write(command.encode() + b"\r")
    process.stdin.flush()
    output = receive()
    assert expected in output, (command, expected, output)


try:
    assert "Boot OK" in receive()
    run("help", "write NAME TEXT")
    run("about", "BETAOS 0.1")
    run("echo hello kernel", "hello kernel\r\n")
    run("ls", "No files.")
    run("write hello booted from scratch", "Saved hello")
    run("cat hello", "booted from scratch\r\n")
    run("ls", "hello  19 bytes")
    run("write hello replacement", "Saved hello")
    run("cat hello", "replacement\r\n")
    run("rm hello", "Removed.")
    run("cat hello", "Error: file not found.")
    for index in range(16):
        run(f"write f{index} data", f"Saved f{index}")
    run("write overflow data", "Error: filesystem full.")
    run("rm f3", "Removed.")
    run("write reused data", "Saved reused")
    run("write " + "x" * 32 + " data", "Usage: write")
    run("x" * 650, "Error: command too long.")
    run("bad-command", "Unknown command")
    run("color z", "Usage: color")
    run("mem", "16 slots")
    time.sleep(1.1)
    process.stdin.write(b"uptime\r")
    process.stdin.flush()
    import re
    match = re.search(r"(\d+) seconds", receive())
    assert match and int(match[1]) >= 1, "Timer IRQ did not advance"
    run("clear", "beta> ")
    process.stdin.write(b"halt\r")
    process.stdin.flush()
    receive(b"System halted.")
    print("PASS: BIOS boot, protected mode, shell, RAM files, limits, timer IRQ, halt")
finally:
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    (ROOT / "build/serial-test.log").write_bytes(transcript)
