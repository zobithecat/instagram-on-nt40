#!/usr/bin/env bash
# tools/run-vm.sh — launch the NT4 guest with all debug hooks on (setup.md §3).
#   COM1 -> host file (app logs)      : vm/debug.log
#   monitor on TCP 5555               : screendump / sendkey from host
#   hostfwd 8080 -> guest 80          : reach a guest server at localhost:8080
# Requires vm/nt4.qcow2 (install NT4 first — see setup.md §2).
set -euo pipefail
cd "$(dirname "$0")/.."

DISK="${DISK:-vm/nt4.qcow2}"
if [[ ! -f "$DISK" ]]; then
  echo "error: $DISK not found. Install NT4 first (setup.md §2), e.g.:" >&2
  echo "  mkdir -p vm && qemu-img create -f qcow2 vm/nt4.qcow2 2G" >&2
  echo "  then boot the installer ISO with -cdrom / -boot d." >&2
  exit 1
fi
mkdir -p vm

# NT4-on-QEMU stability config discovered during install (see docs/vm-notes.md):
#   -cpu 486     : pentium/pentium3 BSOD 0x1E in removable-media class drivers
#                  (sfloppy.sys / cdrom.sys, all deref 0x0127705f). 486 avoids it.
#   -nodefaults  : removes the floppy DRIVE (sfloppy.sys crashes probing it);
#                  we re-add only the devices we need below.
exec qemu-system-i386 \
  -M pc,hpet=off -cpu 486 -m 256 -accel tcg -nodefaults \
  -hda "$DISK" \
  -boot c \
  -device cirrus-vga \
  -netdev user,id=lan,hostfwd=tcp::8080-:80 -device pcnet,netdev=lan \
  -serial file:vm/debug.log \
  -monitor tcp:127.0.0.1:5555,server,nowait \
  -rtc base=localtime \
  -display cocoa \
  "$@"
