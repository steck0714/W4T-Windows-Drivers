#!/bin/sh
# Host checks for uefi/edk2 (no EDK II): the hand-off logic test, then a syntax-only compile of the
# EDK II-facing .c files against uefi/tests/shim. Does not build or run anything on a Switch.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$here/../..; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -O2 "$here/test.c" -o "$tmp/t" && "$tmp/t"
for f in "$root/edk2/GpuHandoff/GpuHandoff.c" "$root/edk2/GpuHandoffApp/GpuHandoffApp.c"; do
  for board in SWITCH_ERISTA_GPU SWITCH_LITE_GPU SWITCH_OLED_GPU; do
    cc -std=gnu11 -fsyntax-only -fshort-wchar -Wall -Wno-unused-parameter -Werror -D$board=1 \
       -I"$root/tests/shim" "$f"
  done
done
echo "syntax OK: GpuHandoff.c, GpuHandoffApp.c (Erista / Lite / OLED)"
