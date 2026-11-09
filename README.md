# BETAOS 0.8

An independent x86-64 operating system written in **C and assembly**. It does not
use Linux, a distribution, or a host operating system inside the OS. It includes
its own freestanding C support and embedded C libraries for fonts, TLS and
JavaScript. Build tools and Python run only on the development machine.

This is a working prototype: it boots through UEFI, provides a graphical desktop,
runs native ASM programs and can install to a compatible SATA disk. Testing uses
QEMU; physical hardware has not been validated.

## Editor and USB applications

The editor supports Ctrl-F search, mouse cursor placement and automatic indentation
on Enter. Ctrl-B builds under the file's name; Ctrl-R builds and runs it. The
[App Kit](docs/APPKIT.md) creates BEX applications on the development machine for
copying to a FAT16/FAT32 USB drive and installing in BETAOS.

The browser runs JavaScript through QuickJS. External/inline script dependencies,
`defer`, `window.onload` and stylesheet ordering have been corrected. Web support
remains partial; [WEB.md](docs/WEB.md) describes its limits.

## Build and boot

Requirements: GCC, GNU binutils with `i386pep`, GNU Make, Python 3, and
mkisofs/genisoimage with EFI El Torito support. Running requires QEMU x86-64 and OVMF.

```sh
make                  # build/BETAOS64.iso and SHA-256
make run              # ISO and a persistent 128 MiB virtual disk
make run-installed    # boot only from that disk after installation
```

The personal disk is `disks/BETAOS.img` and **survives `make clean`**. `make run`
creates it if missing and does not overwrite an existing disk. Disposable test
disks live in `build/`.

The default firmware is `/usr/share/OVMF/x64/OVMF.4m.fd`. Override it with
`make run OVMF=/path/to/OVMF.fd`. This is the combined OVMF image; installations
with separate CODE/VARS files require QEMU pflash options. For tests,
`OVMF=/path/to/OVMF.fd` is an environment variable.

## Desktop

Click the taskbar's `us` indicator to change keyboard layouts, use
Ctrl-Alt-Space, or enter `keymap es` in Terminal. The available layouts are
`us`, `es`, `de` and `fr`; the selection persists on an installed BETA disk.
The Spanish layout includes ñ and AltGr programming symbols. Internal text uses
Latin-1; dead-key accents are inserted literally. The interface is in English.

- The bottom taskbar has **Start**, pinned apps and indicators for open windows.
  Clicking the active app minimizes it; clicking again restores it.
- Windows support minimize, maximize/restore and close. Drag the title bar to move
  a window. Each app retains its position, size and maximized state for the session.
  Windows overlap; clicking an exposed part raises it. Drag the bottom-right corner
  to resize. Minimizing or closing reveals the next window. The taskbar's far-right
  edge shows the desktop. Alt-Tab cycles windows; Ctrl-W closes the current window.
- Double-click a shortcut to open it. Drag desktop icons to move them.
- Right-click for **New file**, Rename, Change icon, Arrange icons and Refresh.
  Change icon cycles seven built-in designs; it does not import images.
- Double-click a file to open the editor. Ctrl-S saves its contents.
- Files provides Open, New, Rename and Delete. Ctrl-N creates a file, Delete asks
  for confirmation, and PgUp/PgDn scroll the list.
- Names, positions, icon designs and files persist on an installed BETA disk.
  Without it, changes stay in RAM and disappear on restart.
- The Windows key opens Start. Escape returns to the desktop or closes the menu.
  The clock uses firmware UTC, falling back to uptime if unavailable. There is no
  time-zone selector yet.

| Key | Application |
| --- | --- |
| F1 | HTML/CSS/JavaScript and HTTPS browser |
| F2 | Terminal |
| F3 | Files and preview |
| F4 | System and installer shortcuts |
| F5 | Rotating colored gears |
| F6 | ASM/text editor |

In Gears, Space pauses or resumes; `+` and `-` change speed.

Start includes a second panel containing **all installed ASM apps**, including
those built in the editor or imported from USB. Click an app to run it and see
its output in Terminal. Previous/Next or PgUp/PgDn move through the list.
Terminal accepts `run NAME` or just `NAME`, with Tab completion. System commands
have priority over apps with the same name; use `run NAME` in that case.

Open Start with the Windows key and type to search installed apps and system
tools. Arrows/Home/End select, Enter launches, and Escape closes. Search preserves
your unfinished terminal command and Files filter. F1–F6 still switch apps.

## Files and USB storage

Files supports name search (`/`, Ctrl-F or Search), size display, Ctrl-C copy and
Ctrl-V paste. Pasting generates a new name and preserves the original. Open/edit,
create, rename and confirmed deletion are available. Local storage is limited to
16 text files of up to 8191 bytes each.

The **Original / Name / Size** button sorts the list. Mouse and keyboard selection
follow that order; USB folders appear first in sorted modes. Home/End select the
first/last result. **Ctrl-PgUp / Ctrl-PgDn** scroll the preview without changing files.

**Ctrl-Z in Local Files** restores the last deleted file, including its icon and
position. Terminal also provides `undelete`. Only one deleted file is retained
in RAM; another deletion replaces it and reboot discards it. Restore never
overwrites an existing name and requires a free slot. Restored files are saved
to an installed BETA disk, but the deletion backup itself is not persisted.

`touch` preserves existing files; `cp`/`mv` reject occupied destination names.
`mv` can rename even when storage is full. `write` and `rm` reject changes to a
file with unsaved editor changes. Rename updates the open editor's filename;
external writes update its buffer when there are no unsaved changes.

**Local**, **USB >** and **Refresh** select storage, cycle detected drives and
scan for new connections. On USB, double-click/Enter opens folders and
Back/Backspace returns. Preview text or binary data, copy text to Local, or
install BEX executables with **Install**. Existing local names are not overwritten.

The native **xHCI + USB Mass Storage Bulk-Only** driver reads **FAT16/FAT32**, MBR,
GPT, long filenames and subdirectories. It runs after ExitBootServices without
firmware reads. USB drives are **read-only**. Preview reads at most 8191 bytes;
BEX import accepts up to 65552 bytes. Text copying retains the local storage
limit. There is no exFAT, NTFS, UAS, USB hub or USB HID support. Validation uses
QEMU; physical hardware remains untested.

```text
usb refresh
usb mount 0
usb ls
usb cd DOCS
usb cat NESTED.TXT
usb back
usb cp README.TXT
usb install FIB.BEX
FIB
```

To attach a USB drive image in QEMU without modifying it:

```sh
make run USB_IMAGE=/path/to/drive.img
```

See [USB storage](docs/USB.md) for details.

## Installers and progress bars

Open **System → Disk Installer** or **Install OS**. Select a disk, confirm erasure,
and click **Install BETAOS**. **The entire selected disk is erased**, including
its partitions. There is no dual boot, resizing or installation preserving data.

The installer creates primary/backup GPT, a 64 MiB FAT32 EFI partition, copies
`EFI/BOOT/BOOTX64.EFI`, verifies readback and saves OS state. Progress bars show
formatting, copying, verification and saving. Installation requires SATA/AHCI,
512-byte logical sectors and at least 128 MiB. NVMe and USB installation targets
are unsupported. After installing in QEMU, close the VM and use
`make run-installed` to boot without the ISO.

The second partition stores checksummed state: files, installed BEX programs and
desktop settings. It is not a general filesystem mountable by other OSes. Saving
is not transactional: interrupted writes may invalidate state; loading checks
the CRC. If several BETA disks exist, the first detected disk is mounted.

Open **ASM Apps** from the desktop, Start or System, or enter `apps` in Terminal.
Buttons install/uninstall bundled packages. Failures show a visible dialog,
including full app storage or an undersized installation disk. Failed app
operations do not save state. Terminal commands include:

```text
programs
install fib
run fib
install memory
run memory
uninstall memory
```

`hello` is installed initially. Apps are actual x86-64 executables, not interpreted
or simulated. They execute in kernel mode and share OS memory. A fault or infinite
loop can stop BETAOS; process isolation is not implemented.

## Integrated editor and assembler

F6 opens a runnable example. Ctrl-S saves; Ctrl-B assembles and installs using the
filename without its extension (`scratch` for `scratch.asm`); Ctrl-R builds and
runs it. Errors include line numbers. Terminal also accepts `edit FILE` and
`asm FILE APP_NAME`.

The editor has 16 undo/redo steps (Ctrl-Z/Ctrl-Y), vertical navigation preserving
the column, Home/End for the line, Ctrl-Home/Ctrl-End for the document, Tab
indentation, automatic indentation, horizontal scrolling and Ctrl-F search.
Before closing or replacing modified text, it offers Save, Discard or Cancel.

The assembler implements an Intel x86-64 **subset**, not full NASM syntax:
64-bit registers, labels, relative branches, basic operations, `lea [rel ...]`,
API calls and `db` data. See the [assembler guide](programs/ASSEMBLER.md) and
[executable format and ABI](programs/README.md).

## Terminal

Up/Down navigate history; Left/Right move the cursor; Home/End jump along the
line. Tab completes, PgUp/PgDn scroll, Ctrl-L clears and Ctrl-C cancels input.
Input survives app switching. Lines exceeding 639 bytes are rejected visibly;
the truncated prefix is never executed.

Commands: `help`, `clear`/`cls`, `about`, `mem`, `net`, `echo`, `ls`/`dir`,
`write NAME TEXT`, `cat`/`type`, `rm`/`del`, `touch`, `cp`, `mv`, `wc`, `pwd`,
`history`, `uptime`, `uname`, `browse URL`, `programs`, `run`, `install`, `uninstall`,
`apps`, `edit`, `asm`, `gears`, `disks`, `installer`, `install-os`, `save`, `load`,
`reboot` and `halt`.

This is a custom command interpreter, **not a complete POSIX shell**. There are
no pipes, redirection, scripting or hierarchical local directories. `/ram` holds
16 text files, each up to 8191 bytes with a 31-character name. File changes are
autosaved to a mounted BETA disk. `save` also stores apps and desktop settings.
There are 16 app slots, each with a native payload of at most 64 KiB.

Terminal installation requires literal confirmation:

```text
disks
install-os
install-os ERASE 0
```

The final command erases **disk 0 as detected by the guest OS**, not a Windows
drive letter or a host path. Use the installer window to inspect the target's
model and capacity before confirming.

## Browser and networking

The browser provides **native HTTPS TLS 1.2**, certificate-chain, hostname and
date validation, HTTP/1.1, chunked responses and redirects. It uses Mozilla public
roots and never disables validation to load a page. It needs valid firmware UTC
and EFI RNG or RDRAND entropy. `make run` enables RDRAND on the virtual CPU.
TLS 1.3 is not implemented.

**Ctrl-L** selects the top address/search bar; Enter navigates. Controls include
Back/Forward (Alt-Left/Alt-Right), Home, Reload, Cancel, session history and mouse
wheel scrolling. Entering `google` or clicking **Google** requests
`https://www.google.com/`. Search queries use Google over HTTPS. The start page
is BETA Browser's own interface, with a Google search form. **Ctrl-R** reloads;
Tab selects links/controls, Enter activates them and Escape cancels loading.
English is requested through HTTP language preferences, navigator.language and
the default search form; websites may choose their own language.

The browser has its own DOM, basic CSS and **real QuickJS** compiled into the OS:
text/colors, margins, padding, rounded borders, GET forms, links, external scripts,
stylesheets/imports, events, timers, Promises and same-origin GET `fetch`. CSS
includes centering, auto margins, containing-block percentages, flex rows/columns,
variables, width rules and `!important`. Hidden inputs are not rendered. GET forms
submit successful controls, with a caret, Ctrl-A selection, Home/End and deletion.
The interface and web pages use antialiased proportional TrueType fonts; Terminal
and Editor use a monospace font. PNG, JPEG, GIF (first frame) and BMP are supported,
with transparency and memory limits. See [browser limits](docs/WEB.md) and
[licenses](THIRD_PARTY.md).

**This is not equivalent to Chrome and does not guarantee compatibility with most
current websites.** Large parts of HTML/CSS and browser APIs, SVG, video, cookies,
JavaScript modules and web storage are absent. Simple pages may work; complex web
apps may render incompletely or fail. Tests validate HTTPS and controlled QEMU
pages; **complete public search has not been validated in this environment**, which
restricts external connections.

The native network stack uses RTL8139, ARP, IPv4, DHCP, DNS A and TCP. Page/resource
loading runs in the background. IPv6, DHCP renewal and complete TCP recovery are
not implemented. To test a local page, run `python3 -m http.server 8080` on the
host and open `http://10.0.2.2:8080/` in BETA Browser.

## Hardware and bootable USB

Requires x86-64, 64-bit UEFI with **Secure Boot disabled**, RGB/BGR GOP of at least
800×600 and a PS/2 keyboard. A PS/2 mouse is optional; USB HID is unsupported.
Networking needs RTL8139 and DHCP. Disk storage needs SATA/AHCI; NVMe, Wi-Fi and
audio are absent. 512 MiB RAM is recommended. Rendering uses a RAM framebuffer,
presents only changed pixel spans and updates the pointer without repainting the
desktop. PS/2 interrupts wake the loop; PIT/HLT reduces idle CPU work. Polling is
used if timer operation cannot be confirmed.

The ISO contains EFI El Torito and GPT/ESP USB boot structures. Write **the entire
image** to a USB drive in raw/DD mode using an image writer; this erases the drive.
Do not copy the ISO as an ordinary file. Firmware can boot it from USB. After
leaving UEFI, storage access needs native xHCI and a supported Bulk-Only device;
USB keyboards still lack a native driver. Legacy BIOS boot exists only in the
32-bit experiment: `make legacy`, `make run32`, `make test32`.

## Screen resolution

Place `BETA.CFG` in the EFI partition's root:

```text
resolution=1280x800
```

Boot selects the closest available RGB/BGR GOP mode. Valid values range from
800x600 to 4096x2160; invalid input retains the 1024x768 preference. Installation
writes and verifies this setting to preserve the resolution when booting from
disk. To build an alternate ISO without changing the default:

```sh
python3 tools/profile_iso.py 800x600
```

## Performance and quality

Local QEMU TCG measurements showed idle CPU falling from about 100% to 2.5–3%.
Regional gear repainting raised measured animation from about 53 to 57 fps and
reduced active CPU from about 100% to 46% in the same configuration. These are
results from that environment, not hardware-independent guarantees. See the
[quality report](qa/QUALITY.md), measurement JSON files and historical
`qa/latest-results.json`. `perf` and `netstats` expose diagnostic counters.
Revision-specific results are in `qa/revision-results.json`; the English-language
update has its own report in `qa/english-results.json`.

## Tests

The full suite requires Pillow (`python-pillow`) for image tests.

```sh
make test                         # assembler and real desktop ISO in QEMU
make test-install                 # install to a virtual disk and boot without ISO
python3 tests/desktop.py --usb     # GPT/USB boot
make qa                           # full suite plus five minutes of repeated activity
```

Tests cover file creation/rename, icon dragging/design changes, maximize/restore,
menus, ASM apps, in-OS editing/assembly, gear animation, DHCP/TCP/HTTP, erasure
confirmation, GPT/CRC, FAT32 and restart persistence. ASan/UBSan exercise the
assembler, loader, protocols and renderer with deterministic mutations and partial
versus full repaint comparisons. Deliberately faulting ASM apps verify exception
frames (vector, code, RIP and CR2), not process isolation. Test disks such as
`build/installed-os.img`, screenshots and logs stay in `build/`; tests never select
a physical host disk.

Main sources: `desktop/boot.c`, `gui.c`, `input.c`, `net.c`, `program.c`,
`assembler.c`, `storage.c`, `usb.c`, `fat.c` and `desktop/cpu.S`. This remains a
prototype without user processes, memory protection, a scheduler, SMP, universal
drivers or Internet package updates.

Boot follows [UEFI](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html);
the SATA driver follows [Intel AHCI](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1_2.pdf).
