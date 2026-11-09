# Creating apps for USB

The kit uses GNU binutils on the development machine. It produces x86-64 machine
code with a BEX header, executed directly by BETAOS.

```sh
python3 tools/appkit.py new build/myapp
# Edit build/myapp/main.S
python3 tools/appkit.py build build/myapp
```

Copy `build/myapp/myapp.bex` to a FAT16/FAT32 USB drive from your computer.
Eject it normally before connecting it to BETAOS. In the OS terminal:

```text
usb refresh
usb ls
usb install myapp.bex
run myapp
```

You can also select the BEX in Files → USB and click Install. Installed apps
appear in Start. BETAOS reads the USB drive; it does not write or format it.
The kit does not touch physical devices or require rebuilding the ISO.

`main.S` uses GNU AT&T syntax. The editor's integrated assembler uses the Intel
subset documented in [ASSEMBLER.md](../programs/ASSEMBLER.md). The source files
are not interchangeable; both assemblers produce the same BEX format.

Inside BETAOS, F6 opens the editor. Ctrl-F searches (Enter finds the next match;
Esc returns to editing), the mouse positions the cursor, Enter preserves
indentation and Ctrl-Z undoes a complete insertion. Ctrl-B builds and installs
under the filename without its extension; Ctrl-R also runs the app. Ctrl-S saves
the source. Limits remain 8191 bytes per source file and 64 KiB per executable
payload. Programs run in kernel mode without isolation.

Reproducible test: `python3 tests/appkit_workflow.py`. It creates and builds a
project, places it in a test FAT image, and installs/runs it through the native
USB driver in QEMU. It does not use the user's personal disk.
