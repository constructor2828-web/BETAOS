# Native USB storage

`desktop/usb.c` implements xHCI and USB Mass Storage Bulk-Only in freestanding C.
`desktop/fat.c` reads FAT16/FAT32 and their directories. The driver starts after
leaving UEFI, without firmware services or Linux.

## Scope

- First detected PCI xHCI controller; devices directly connected to root ports.
  Up to four simultaneous drives.
- USB Mass Storage class 08, SCSI subclass 06, Bulk-Only protocol 50, LUN 0.
- SCSI READ CAPACITY(10), 512-byte sectors, capacity below 2 TiB.
- FAT16/FAT32 volumes without a partition table, with primary MBR partitions,
  or with GPT.
- 8.3 and long filenames, directories, text and hexadecimal preview.
- Read-only: no SCSI write commands or formatting.
- Refresh / `usb refresh` detects new connections; mounting is not automatic.
- Disconnecting invalidates the media identity. Reconnect, refresh and mount
  again; an old selection never silently reads a replacement disk.
- A timeout invalidates the controller until reboot so DMA memory is not reused
  while a transfer could still be pending.

There is no EHCI/UHCI/OHCI, hub, UAS, multiple-LUN, exFAT/NTFS, 4 KiB sector or
USB keyboard/mouse support. The SATA installer cannot install the OS to USB.
Firmware can boot the ISO from USB independently of these native-driver limits.

## Files

The list shows up to 96 entries and allows up to 15 nested directories. Preview
shows up to 8191 text bytes or the first 512 binary bytes in hexadecimal; the
original file can be larger. Copy to Local accepts Latin-1 text up to 8191 bytes,
with 16 local file slots. App import accepts BEX ABI 1 up to 65552 bytes.
The app name is taken from the filename before its first dot and must contain
1–31 ASCII letters, digits or underscores. Existing installed apps are not replaced.

Reads are synchronous with bounded waits per transfer. The reader checks device
and volume bounds, cluster chains and media changes. This is not a universal USB
stack or a guarantee of physical hardware compatibility.

## Verification

`tests/usb_storage.py` exercises reads and navigation in the real kernel, copies a
local file, installs/runs a BEX through Files, disconnects/reconnects the device,
and checks that the USB image is unchanged. QEMU also opens the backend read-only.

`tests/usb_variants.py` covers FAT16/FAT32, MBR/GPT, unpartitioned volumes and
reading the same USB device used by firmware to boot. `make test-fat` runs
ASan/UBSan on FAT and 5000 BPB mutations. `make qa` includes these checks alongside
desktop and installation regressions.

Support is validated against QEMU's `qemu-xhci` storage emulation. No physical USB
drive has been connected in this development session.

Specifications: [Intel xHCI](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)
and [USB-IF Mass Storage Bulk-Only](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
