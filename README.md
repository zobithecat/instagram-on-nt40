# instagram-on-nt40

A **native Instagram client for Windows NT 4.0**, built from scratch in C — no
browser, no GUI framework, no OS-native TLS. Cross-compiled from macOS
(Apple Silicon) and run under QEMU x86 emulation.

- **Rendering**: a 32bpp DIB-section compositor with hand-written alpha blending
  and custom owner-drawn controls, straight on top of Win32 GDI.
- **Networking** (planned): Winsock2 + async + a hand-rolled HTTP/WebSocket client.
- **Crypto** (planned): bundled **mbedTLS** for TLS 1.2 + SNI (NT4's SChannel
  can't do modern ciphers / TLS 1.2), entropy from NT4 `CryptGenRandom`.

See [native.md](native.md) for the full architecture and [setup.md](setup.md)
for the QEMU / toolchain environment.

## Layout

| dir | role | tested |
|-----|------|--------|
| `core/` | OS-independent pure C: `raster` (DIB pixels, alpha, downscale); later `http` `ws` `json` `model` | Mac clang + ASan/UBSan |
| `pal/`  | thin Win32 wrapper: window, DIB-section double buffer, COM1 logging | NT4 integration |
| `ui/`   | NT4 GDI: the gray-bevel feed compositor & custom controls | NT4 integration |
| `net/` `img/` `source/` | TLS/HTTP glue, image decode, pluggable data sources | (upcoming) |
| `tools/` | VM run/capture/transfer scripts, host render preview | — |

The strategy is to keep the **pure core large** and NT4-specific code thin, so
most logic is verified at full speed on the Mac; the VM is for final integration.

## Build

Requires `brew install qemu mingw-w64 cmake imagemagick wireshark` and Xcode CLT.

```bash
make test      # build core/ + tests with clang + ASan/UBSan, run them
make nt4       # cross-compile the NT4 app -> dist/app.exe (i686-w64-mingw32)
make preview   # render the feed offscreen on the host -> build/feed.png
```

`make preview` renders the *exact* framebuffer the NT4 app produces (the feed is
pure `core/raster`), so you can iterate on the look without booting the VM.

## Running on NT4 (QEMU)

1. Install NT4 once into `vm/nt4.qcow2` (SP6a + AMD PCnet driver) — see
   [setup.md](setup.md) §2. **This is a manual step** (needs an NT4 ISO).
2. Push a build and launch with debug hooks:
   ```bash
   make nt4
   ./tools/serve-ftp.sh &          # host FTP on :2121 serving ./dist
   ./tools/run-vm.sh               # COM1->vm/debug.log, monitor on :5555
   # in NT4:  ftp -> open 10.0.2.2 2121 -> binary -> get app.exe -> run it
   ./tools/screendump.sh vm/nt4.png   # capture the real guest render
   tail -f vm/debug.log            # app's COM1 debug log
   ```

## NT4 targeting rules (enforced)

`-D_WIN32_WINNT=0x0400 -DWINVER=0x0400` caps the API surface; linker flags stamp
the PE at subsystem/OS version 4.0; `-static -static-libgcc` drops the libgcc DLL
dependency. No `AlphaBlend`/`UpdateLayeredWindow`/visual-styles — alpha is done in
`core/raster`. Verify a build with:

```bash
make nt4 && python3 - <<'PY'
import struct; d=open('dist/app.exe','rb').read(); o=struct.unpack_from('<I',d,0x3c)[0]+24
print("subsystem", struct.unpack_from('<H',d,o+68)[0], "(2=GUI)",
      "os", struct.unpack_from('<HH',d,o+40), "subsysver", struct.unpack_from('<HH',d,o+48))
PY
```

## Status

- [x] Milestone 0 — repo, two-target build, `core/raster` + ASan tests, host preview
- [x] **Milestone 1 — runs on real NT4**: DIB compositor + double buffer, freestanding
      (no-CRT) `app.exe` loads and shows its window under QEMU. `docs/screenshots/`.
- [x] **Milestone 2 — gray-bevel feed** renders on NT4 in truecolor (Cirrus @ 1280x1024)
      — smooth gradient photos, action bar, nav.
- [x] **Milestone 2.1 — real text**: 5x7 bitmap font in `core/font` (Instagram
      wordmark, usernames, locations, likes, captions, comments) verified on NT4.
- [x] **Milestone 3 — image decode**: from-scratch **QOI** codec in `img/qoi`
      (no JPEG library needed, freestanding-friendly). The client loads `.qoi`
      photos off the CD (`pal_read_file`), decodes + area-downscales them into the
      feed. Sample scenes authored with `tools/mkqoi`. Verified on NT4.
- [ ] Milestone 3.1 — scrollable feed; real JPEG decoder behind the same API
- [ ] Milestone 4 — `core/http`+`json`+`ws` over `pal` sockets to a local mock server
- [ ] Milestone 5 — bundle mbedTLS, TLS 1.2 + SNI to a self-signed HTTPS mock
- [ ] Milestone 6 — `GraphApiSource` (official API), pagination, post detail

### Notable NT4 gotchas solved (see `docs/vm-notes.md`)
- **Install BSOD 0x1E** (removable-media class drivers) → `-cpu 486 -nodefaults`.
- **"unable to load DLL"** → Homebrew mingw links the UCRT (absent on NT4). Build
  **freestanding** (`-nostdlib` + `pal/nt4_crt.c` own entry/heap/mem, `wvsprintfA`
  for logs) so the exe imports only kernel32/user32/gdi32. Also kills all CMOV.
- **Gradient banding** → 640×480×16-color default; install the Cirrus driver for truecolor.
