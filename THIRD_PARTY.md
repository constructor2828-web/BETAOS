# Embedded components

BETAOS remains an independent C/ASM kernel. These portable source libraries are
compiled into its EFI image; they do not add a Linux runtime or host system calls.
Upstream license files are retained in the indicated directories. UPSTREAM.json
files record source revisions and Git blob identifiers.

| Component | Source / license | Integration |
| --- | --- | --- |
| BearSSL | https://bearssl.org/ and status-im/BearSSL, commit `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`; MIT, `vendor/bearssl/LICENSE.txt` | Portable C TLS and X.509, no upstream source changes; OS supplies verified time and random seed. |
| QuickJS | https://github.com/bellard/quickjs, commit `092277ac1eb2997916a8ad0c4fbf961c91180498`; MIT, `vendor/quickjs/LICENSE` | `BETAOS` disables CONFIG_ATOMICS and makes its waiter cleanup a no-op. Threads/Atomics are unavailable; runtime stack checks remain enabled. Own DOM bridge and freestanding C support. |
| OpenLibm | https://github.com/JuliaMath/openlibm, commit `5fe399749f9276eaa0b8403e507470da05cbbb3f`; multiple BSD/MIT/public-domain notices in `vendor/openlibm/LICENSE.md` and source files | Portable mathematical routines required by QuickJS/font rendering; selected double precision C source archive, no host libm. |
| stb_truetype / stb_sprintf / stb_image | https://github.com/nothings/stb, commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`; MIT/public-domain alternatives in each header | Font rasterizer, bounded PNG/JPEG/GIF/BMP decoding and trusted C formatting strings. No font downloads or arbitrary web-font parsing. |
| iA Writer Mono S Regular | https://github.com/iaolo/iA-Fonts; SIL OFL 1.1, `assets/fonts/LICENSE.md` | Bundled `assets/fonts/Desktop.ttf`, embedded unchanged. |
| Liberation Sans Regular | https://github.com/liberationfonts/liberation-fonts; SIL OFL 1.1, `assets/fonts/Interface-LICENSE.txt` | Unmodified bundled `assets/fonts/Interface.ttf`, used for interface labels and proportional web text. |
| Mozilla CA root bundle | https://curl.se/docs/caextract.html, retrieved from https://github.com/bagder/ca-bundle (`ca-bundle.crt`), snapshot dated 2026-10-20 in bundle header | `vendor/certs/roots.pem`; Mozilla Public License 2.0, https://www.mozilla.org/MPL/2.0/. `tools/tls_roots.py` converts it into BearSSL trust anchors. Refresh and rebuild to update roots. |

The build also statically links GCC's compiler arithmetic support (`libgcc`) under
its GCC Runtime Library Exception. No glibc or host shared libraries are linked.
Test certificates under build/ are generated locally and are never trusted by the
production trust store. The repository does not contain private keys for public CAs.
