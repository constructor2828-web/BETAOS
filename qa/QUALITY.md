# BETAOS 0.8 quality and performance work

This report describes tested improvements. It does not claim the absence of every
possible defect, production readiness, or universal hardware compatibility.

## Rendering and responsiveness

The initial desktop repainted and copied the whole framebuffer for pointer motion
and continuously polled while idle. The current implementation:

- Restores and paints only the pointer's small rectangle for ordinary mouse motion.
- Compares the RAM canvas with a previous-frame buffer and presents changed row spans.
- Redraws just the bounded gear area on animation-only frames; full redraw remains the
  reference path for window, dialog, menu, clock and other scene changes.
- Uses scanline fills for circles and triangles, time-based animation and a 60 Hz target.
- Uses PIT, keyboard and mouse interrupts to wake an idle `HLT` loop. The timer rate
  adapts between idle and animation; hardware that fails the timer probe uses polling.
- Advances browser ARP/DNS/TCP/HTTP work as bounded state transitions, keeping input,
  other apps and animation responsive during slow network responses.

Local single-run measurements used QEMU TCG, q35, 512 MiB RAM, headless GOP at
1024×768. Earlier and later samples can vary with host load:

| Measurement | Earlier path | Optimized path |
| --- | --- | --- |
| Idle QEMU host CPU | 99.98% | about 2.5–3% |
| Active gear animation CPU | 99.92% | 46.12% |
| Gear animation rate | 53.33 fps | 57.18 fps |

Raw samples are in `performance-before.json`, `performance-before-animation-cache.json`,
`performance-after-animation-cache.json` and `performance-final.json`. Command-response
samples include QMP injection and 5 ms log polling, so they are approximate and not
microsecond latency measurements. The final benchmark runs without other QA VMs.

A cursor correctness test compares scene pixels before and after a pointer path,
excluding only the pointer boxes and clock. Thirty pointer events added only six
UI frames and roughly 74–84 KiB of video writes, including the typed diagnostic
command, instead of requiring thirty full-screen copies.

## Defects found and fixed

The 0.6 daily-use pass also fixes destructive `touch`, silent command destination
replacement, full-store rename failure and stale editor buffers/names after
terminal writes or renames. Terminal mutations that are rejected no longer trigger
an unnecessary state save. Empty file/app lookups cannot resolve an unused slot.

New desktop interactions include Start search and keyboard launch, file ordering
with matching mouse/keyboard selection, scrollable previews, Ctrl-F search and
one-session/one-file delete recovery that preserves icon geometry. A dedicated
QEMU workflow covers command data preservation, modified editor guards, recovery
name conflicts, full-store rename and keyboard focus across menus/apps. Render
sanitizers cover preview bounds, ordered USB entries and empty search results.

- Sticky modifier state when Shift was released while Ctrl was held; right Ctrl,
  Caps Lock, Pause and PrintScreen handling also have direct decoder tests.
- Unaligned wider C accesses in executable patching and serialized app/state data.
  ASan/UBSan reproduced the assembler issue; byte-based little-endian helpers now
  cover these accesses, including FAT and app headers.
- Duplicate allocation of a boot framebuffer cache, found during initialization review.
- Duplicate and empty app names accepted during saved-app import. Import now validates
  the whole record set before changing installed apps.
- Silent acceptance of an assembler entry label beyond its executable payload, and
  unsafe handling of a zero-length diagnostic buffer.
- Receive-buffer mismatch between RTL8139 NoWrap configuration and wrapped software
  reads. Packets that crossed 8 KiB could corrupt and truncate larger HTTP responses.
  The driver now reads the configured contiguous spill area. This behavior was checked
  against the primary [QEMU RTL8139 receive implementation](https://qemu.googlesource.com/qemu/+/refs/tags/v8.1.2/hw/net/rtl8139.c).
- Synchronous browser waits that blocked the desktop, inconsistent Content-Length
  parsing, and noisy partial-response success logging. Loading is asynchronous,
  complete/partial results are distinct, and malformed/unsupported encodings are rejected.
- Repainting work and lack of per-app window geometry that made movement rough.
- Incomplete editor navigation, missing undo/redo and missing unsaved-change guards.
- File-list/panel overflow at smaller display sizes and misleading RAM-only labels
  on an installed disk. Lists are bounded and scrollable and storage state is explicit.
- Silent truncation of overlong command/URL input. Overflow survives app switching
  and rejects execution with visible feedback.
- Invisible errors when the native app store was full or an installation disk
  was unsuitable. Error dialogs now explain rejection, failed app operations no
  longer trigger state saves, and capacity recovery is tested.
- A QA-harness offset bug: raw UART byte offsets must not be applied after universal
  newline translation. The harness now searches bytes at the recorded offsets.

## Validation

The later keyboard/apps/USB extension adds:

- US/ES/DE/FR maps, AltGr, logical shortcuts, a taskbar selector, and keyboard
  settings that survive disk-only boot while preserving older saved states.
- Direct installed-app commands and a paged Start menu for installed/compiled/
  USB-imported BEX programs.
- Files search, clipboard duplication, metadata, a local/USB source selector,
  folder navigation, text/hex previews, local import and BEX installation.
- Native xHCI, USB Bulk-Only/SCSI reads, FAT16/FAT32, superfloppy/MBR/GPT and LFN.
- Real kernel tests with read-only emulated USB media: boot-controller ownership,
  maximum-size BEX import/execution, repeated hotplug and media identity guards.
- FAT ASan/UBSan tests with malformed chains, invalid LFN order, media changes and
  5,000 deterministic BPB mutations. No host physical device is accessed.

`make qa` runs ordered checks and writes `latest-results.json` with commands,
exit statuses, durations, log paths and a source-tree SHA-256. The final source
hash must match the hash at the start of the run. Individual logs are in `logs/`.
The manifest is authoritative for the latest run's completion status.

Final 0.6 run: all 28 stages passed at 2026-11-08T23:32:14.784970+00:00 with unchanged source
hash `62bd889e45fd3f0748987ff21dd5393f851f7697046604fcfdcdf888cb92c47b`. The 300.64-second soak completed
99 iterations, 460 commands and 124 native returns with zero panic/fatal markers.
The final local QEMU sample measured 57.64 fps, 47.55% active CPU and
3.0% idle CPU. These samples vary with host load.

A separate native preview check after QA compared the actual preview pixels before
and after Ctrl-PageDown; the content changed. Screenshot capture waits for redraw,
since a screendump taken immediately after rapid injected keys can show an older frame.


The checks cover:

- Native ISO, USB/GPT and installed hard-disk boot without installation media.
- GPT primary/backup CRCs, FAT32 boot file byte equality and saved-state CRCs.
- Persistence of files, compiled apps, icon positions/names/styles and display preference.
- Rejection of corrupt CRCs, oversized state headers and unknown desktop schemas.
- Actual PS/2 input, fast typing, overflow rejection, app/window switching and dragging.
- Full-app-store failure/recovery and undersized-disk rejection without disk modification.
- Editor navigation, indentation, undo/redo, save/discard/cancel and file management.
- Native executable validation, CPU execution and callbacks through the BEX API.
- 5,000 deterministic saved-app mutations, 20,000 assembler mutations and 20,000
  DNS/HTTP/IPv4/TCP mutations under address and undefined-behavior sanitizers.
- RGB/BGR rendering with odd strides at 800×600, 1024×768 and 1920×1080, guarded
  buffers, clipped primitives and cached-animation/full-renderer comparisons.
- Native GOP boot at 800×600, 1280×800 and 1920×1080, plus unavailable-mode fallback.
- Repeated large HTTP transfers across receive-ring boundaries with no checksum
  corruption, slow-response background activity and cancellation.
- Correct diagnostic frames for intentionally generated UD2 and page faults in a
  disposable native app. Such privileged apps are still able to halt the kernel.
- A five-minute mixed desktop interaction soak, reported in `soak-results.json`.

LeakSanitizer is disabled for host-side sanitizer tests because it cannot run under
the environment's ptrace restrictions. AddressSanitizer and UndefinedBehaviorSanitizer
remain enabled. Most core modules use static storage. The embedded JavaScript/font runtime now
uses a bounded native 32 MiB heap; it does not use a host allocator in the OS.

## Practical limits

Physical hardware has not been validated. GOP, PS/2, RTL8139, SATA/AHCI and direct
xHCI Bulk-Only USB storage are the supported device paths; USB HID, hubs, UAS,
EHCI/UHCI/OHCI, NVMe, Wi-Fi, audio and a general GPU driver remain
outside this prototype. The terminal is not a complete POSIX shell, the assembler
implements a documented subset, and the browser implements a bounded HTML/CSS/JavaScript subset with TLS 1.2,
not a complete modern web platform. Public Google rendering remains unverified. Native apps share kernel privilege and memory and run synchronously. Saved
state is checksummed but not transactional across power loss. The taskbar clock
shows uptime. Keyboard text is Latin-1 and dead accents are literal. USB filesystems
are read-only FAT16/FAT32; exFAT and NTFS are unsupported. See `../docs/USB.md` for
transport, directory and transfer bounds. These are explicit limits, not
capabilities implied by passing tests.

## Native web and font extension (0.7)

TLS 1.2 runs inside the guest with chain, hostname and firmware-time checks. A
separate test ISO trusts a locally generated CA; production rejects that CA. Tests
cover encrypted chunked HTTP, wrong host and invalid certificate dates. QuickJS
runs native ECMAScript, DOM mutation, external scripts/styles, timers, fetch JSON,
keyboard controls and GET forms. Infinite JavaScript is interrupted and terminal
interaction is verified afterward. TrueType glyphs are rasterized with grayscale
antialiasing, with real-font RGB/BGR clipping checks under sanitizers. Malformed
HTML tests exercise bounded parsing/layout and DOM cycle/origin guards.

The source manifest includes vendored source, provenance JSON, CA bundle and font
assets. See `../docs/WEB.md` for exact platform limits. The earlier 0.6 measurements
above are historical; `latest-results.json` and `performance-final.json` describe
the current run when completed.

The first complete 0.7 run passed all 32 stages at 2026-11-09T08:16:51Z, with
300.86 seconds of mixed activity, 460 commands and 124 native returns, and no
panic/fatal markers (`web-07-full-results.json`, `web-07-full-soak.json`). The ISO
packaging was then extended to include licenses and browser documentation. The
final packaging validation repeats the full suite with a 60-second activity pass;
`latest-results.json` binds that run to the final source tree. The local rendering
sample before repackaging was 56.68 fps and 3.0% idle host CPU.

## Desktop and browser usability (0.8)

The desktop now draws real inactive windows below the focused app. Exposed-window
clicks raise them, minimizing/closing reveals another visible window, and a corner
grip resizes each app independently. The browser can shrink to a 400-pixel window.
A proportional interface font, grayscale rounded corners, readable icon labels,
updated wallpaper and conventional app titles replace the earlier sparse styling.

The browser has a top address/search entry, back/forward history, reload/stop/home,
selection/cursor editing, scroll bar and native IntelliMouse wheel negotiation.
CSS fixes cover hidden inputs overridden by author rules, selector attributes and
combinators, importance, variables, relative widths, centered auto margins, flex
rows/columns, viewport media conditions, clipped backgrounds and proportional
text measurement. Layout no longer consumes DOM arena memory on each repaint.

Google's user-provided screenshot showed actual loaded HTML but visible hidden
fields and missing images/centering. A local structural reproduction now checks
these defects, real PNG pixels, CSS imports, field editing, successful GET fields
and history. The development VM's public Google request still fails at DNS; local
fixture validation is not a claim of live Google search compatibility. PNG/JPEG/GIF
codecs also run against deterministic malformed inputs under ASan/UBSan. Exact
limits remain documented in `../docs/WEB.md`. `latest-results.json` records the
final run, with .inc modules and both fonts included in the source hash.

The hotplug harness now waits for the prior `usb refresh` to complete before
attaching replacement media. Previously the new device could be discovered during
the earlier scan, making the test wait for a second discovery that should not
occur. Native unplug/reconnect then passes with the read-only backend. The wheel
check uses explicit QMP wheel-down events and compares viewport pixels, rather
than relying on HMP dz direction or clock changes outside the viewport. Rapid
wheel batches cover signed 8-bit IntelliMouse deltas. Long address editing is checked against the actual HTTP request path. The
browser also resets its scroll when the displayed URL changes.
