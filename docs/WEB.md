# BETA Browser 0.8

Native C browser with embedded BearSSL and QuickJS. No host browser, Linux kernel,
remote rendering service or TLS termination proxy is part of the OS.

## Everyday navigation

F1 opens the browser. Click the top address field or press Ctrl-L, type a URL or
search, then Enter. Bare domains use HTTPS; ordinary text requests Google's search
endpoint. The start page has its own Google search form and links to public sites.
The Google button requests the real Google homepage. Back/forward buttons and
Alt-Left/Alt-Right traverse session history; home opens the local start page.
Reload/stop, the wheel, PageUp/PageDown and the scroll bar control loading/viewing.

Fields support left/right, Home/End, Delete/Backspace, Ctrl-A, UTF-8 form encoding,
a visible caret, placeholders, password masking and bounded maxlength. Tab moves
between controls and brings the focused control into view. Enter submits GET forms;
submit buttons other than the activated one, disabled controls, and unchecked
checkbox/radio fields are excluded. Hidden inputs remain successful fields but are
never rendered, even if CSS targets all inputs. POST forms are explicitly rejected.
Textareas currently use the single-line form-control behavior.

## Implemented rendering

- Bounded HTML tokenizer/DOM, text/entities, block and inline flow, links and forms.
- Proportional TrueType text and interface labels; monospace preformatted content.
- PNG, JPEG, BMP and the first GIF frame; alpha blending, aspect-preserving default
  sizing and clipped painting. Image dimensions/memory are checked before decoding.
- External and inline CSS, relative stylesheet imports, tag/class/ID/attribute
  selectors, descendant/child/sibling combinators, basic :root/:focus/:not and
  first/last-child matching, specificity and normal/important declaration passes.
- Inherited custom properties with var() fallbacks and bounded expansion; colors,
  sizes, line height, borders, radius, padding, percentages, content/border boxes,
  min/max width, centered auto margins and text alignment. Basic flex row/column,
  grow, gaps, alignment and wrapping; viewport min/max width/height media rules.
- Backgrounds, glyphs and images clipped to the viewport during scrolling.
- Upstream QuickJS: modern ECMAScript syntax, arrays/objects, BigInt, JSON, Promises,
  ordinary inline/external scripts, limited document/element creation and mutation,
  attributes, style/classList setters, events, timers, location and same-origin
  GET fetch with text/JSON responses.

## Transport

Native RTL8139, ARP, IPv4, DHCP, DNS A and TCP. Segmented/retried requests,
HTTP/1.1 GET, content lengths, identity encoding, chunk decoding and eight redirects.
TLS 1.2 uses SNI, Mozilla roots and chain/hostname/date checks; missing firmware UTC
or EFI RNG/RDRAND fails closed. make run exposes RDRAND to QEMU. No insecure TLS
fallback is used, and the test CA is trusted only in the separate QA image.

## Limits

This is not a complete standards browser or a replacement for Chrome/Firefox.
No SVG, CSS background images, video, canvas/WebGL, audio, cookies, localStorage,
service workers, ES modules, XHR, general POST/upload forms, TLS 1.3, HTTP/2 or IPv6.
Compressed responses are rejected. HTML5 parsing, table/flex layout, inline boxes,
CSS functions/pseudos/cascade, URL handling, event semantics and script ordering
remain partial. Positioning, grid, font downloads, animations and many browser APIs
are absent. Unknown APIs can throw; @supports is ignored. Imports have bounded
loading and simplified ordering; media queries support a subset, not every Boolean
condition. InnerHTML's getter returns text rather than serialized markup.

Per navigation: 1 MiB HTTP buffer, 2,048 nodes, 8,192 attributes, 3 MiB DOM strings,
256 CSS rules, 512 KiB parsed rule storage, 24 sheets, 8,192 layout boxes, up to 256
items per flex container, 96 resource requests, 32 images, 128 listeners, 32 timers.
Images are capped at 2,048 pixels per dimension, 1,048,576 total pixels each and
8 MiB decoded cache. The shared C heap is 32 MiB; QuickJS has a 12 MiB memory limit,
128 KiB stack and 200 ms execution budget per batch. Mixed HTTP assets in HTTPS
pages are rejected; fetch is same-origin GET, with no implemented CORS mechanism.

The browser and VM share kernel memory. These bounds do not provide process
isolation or certify security. Physical hardware is unverified.

## Validation and Google

The user's Google screenshot proved public HTML arrived on their machine; it also
exposed hidden-input, image and layout defects. tests/browser_comfort.py reproduces
those structures on a controlled real HTTP server and checks native PNG pixels,
stylesheet imports, editing/GET, wheel and history. It is a fixture, not a saved or
simulated Google result page. The development VM's live Google request fails during
DNS lookup, so public Google search results/consent pages have not been verified.
No claim of majority-site compatibility follows from the local tests.

tests/web_native.py validates actual QuickJS/DOM/CSS, external resources, timers,
fetch JSON and recovery after an infinite script. tests/https_native.py validates
real guest TLS, chunked HTTP and rejection of wrong-host/untrusted/out-of-date
certificates. Host ASan/UBSan tests cover fonts, DOM/CSS mutations and malformed
images. The complete source-bound run is recorded in qa/latest-results.json.

## Loading and editor revision

Classic external scripts now finish before subsequent inline scripts execute.
External `defer` scripts run in document order after ordinary scripts. Async
scripts still use the serial transport; this is not full HTML parser scheduling.
A failed external request releases the script queue. `document.readyState` moves
from loading through interactive to complete, and `window.onload` is called.
`noscript` fallback content is hidden while the JavaScript runtime is enabled.
External stylesheets retain their position relative to inline styles instead of
winning just because they downloaded later. Attached style elements created or
updated by JavaScript are reparsed. CSS imports and dynamic script insertion
remain incomplete. These changes do not establish full web compatibility.

The previous full-suite report predates this revision. Revision-specific checks
are recorded separately in `qa/revision-results.json`.
