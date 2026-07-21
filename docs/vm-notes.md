# NT4-on-QEMU install notes (empirical)

QEMU 11.0 on macOS/Apple Silicon (TCG), NT 4.0 Workstation RTM (build 1381,
volume `NTWKS40A`), CD key in `iso/Key.txt`.

## The install-time BSOD hunt (STOP 0x0000001E)

Booting the installer with the setup.md defaults (`-cpu pentium3`, default
machine) BSODs during text-mode setup. Three successive crashes, each
`KMODE_EXCEPTION_NOT_HANDLED (0x1E)` **dereferencing the same address
`0x0127705f`**:

| # | `-cpu`   | machine        | faulting driver | fixed by |
|---|----------|----------------|-----------------|----------|
| 1 | pentium3 | default        | `sfloppy.sys`   | → CPU change didn't help |
| 2 | pentium  | default        | `sfloppy.sys`   | (same crash, same addr)  |
| 3 | pentium  | `-nodefaults`  | `cdrom.sys`     | floppy gone, next class driver crashes |
| ✓ | **486**  | `-nodefaults`  | — none —        | **boots to "Welcome to Setup"** |

Key observations that led to the fix:
- The **fixed disk** (`disk.sys`) never crashed — only **removable-media class
  drivers** (`sfloppy.sys`, `cdrom.sys`), which share `CLASS2.SYS`. The bug is in
  that shared removable-media init path, not the individual driver.
- The **accessed pointer was identical** (`0x0127705f`) across every crash and
  across CPU models → not a per-CPU-feature codepath, but the same bad pointer
  computed the same way each run.
- `-nodefaults` removes the floppy **drive** (keeps the controller), which got us
  past `sfloppy.sys`; then `cdrom.sys` hit the same wall.
- Dropping to **`-cpu 486`** (NT4's true baseline) clears it entirely — the
  Pentium-era init path in the class layer is what QEMU's TCG trips.

## Working config

Install (boot from CD):
```
qemu-system-i386 -M pc,hpet=off -cpu 486 -m 128 -accel tcg -nodefaults \
  -hda vm/nt4.qcow2 -cdrom "iso/Windows NT 4.iso" -boot d \
  -device VGA -netdev user,id=lan -device pcnet,netdev=lan \
  -serial file:vm/debug.log -monitor tcp:127.0.0.1:5555,server,nowait \
  -rtc base=localtime -display cocoa
```
Run after install: same but `-boot c`, no `-cdrom` (see `tools/run-vm.sh`).

- After text-mode file copy, setup reboots. With `-boot d` it would re-enter
  setup from the CD → switch the next boot to disk via the monitor:
  `printf 'boot_set c\n' | nc -w1 127.0.0.1 5555` (or relaunch with `-boot c`,
  keeping `-cdrom` attached — GUI setup still needs files off the CD).

## Driving setup headlessly

Monitor on TCP 5555; capture with `screendump`, type with `sendkey`:
```
printf 'screendump vm/nt4.ppm\n' | nc -w2 127.0.0.1 5555 ; magick vm/nt4.ppm vm/nt4.png
printf 'sendkey ret\n'  | nc -w1 127.0.0.1 5555     # Enter
printf 'sendkey f8\n'   | nc -w1 127.0.0.1 5555     # accept EULA (after pgdn x8)
printf 'sendkey ctrl-alt-delete\n' | nc -w1 127.0.0.1 5555   # login screen CAD
```
Text-setup key path: ENTER (setup) → ENTER (storage) → C (new disk) →
pgdn×8, F8 (EULA) → ENTER (hardware) → ENTER (unpartitioned) → ↓,ENTER (NTFS) →
ENTER (\WINNT) → ESC (skip exam) → file copy → reboot.
