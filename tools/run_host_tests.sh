#!/usr/bin/env bash
# Host-side tests only (static source checks + pure-C logic). They do not need a WDK and do
# not prove a driver runs on a Switch.   usage: tools/run_host_tests.sh   (from repo root)
cd "$(dirname "$0")/.." || exit 2
pass=0; fail=0; tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
run() { local name=$1; shift
  if (cd "$(dirname "$name")" && "$@") >"$tmp/out" 2>&1; then pass=$((pass+1)); echo "PASS $name"
  else fail=$((fail+1)); echo "FAIL $name"; sed 's/^/     /' "$tmp/out" | tail -8; fi; }
# Some tests compile with -fsanitize=address,undefined; if this machine has no sanitizer
# runtime, shim clang so the flag is dropped (the assertions still run, only ASan is lost).
if ! echo 'int main(void){return 0;}' | clang -x c -fsanitize=address,undefined - -o "$tmp/asan" >/dev/null 2>&1; then
  mkdir -p "$tmp/shim"; real=$(command -v clang)
  printf '#!/bin/sh\nargs=""; for a in "$@"; do case "$a" in -fsanitize=*) ;; *) args="$args \"$a\"";; esac; done\neval exec "%s" $args\n' "$real" > "$tmp/shim/clang"
  chmod +x "$tmp/shim/clang"; export PATH="$tmp/shim:$PATH"; echo "note: no sanitizer runtime here, running without ASan/UBSan"
fi
python3 -I tools/check_hwid_matrix.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_hwid_matrix.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_hwid_matrix.py"; tail -5 "$tmp/o"; }
for t in $(find drivers/t210-icosa/drivers -path '*tests/*.py' -not -path '*/TegraGpu/*'); do run "$t" python3 -I "$(basename "$t")"; done
for t in $(find drivers/t210-icosa/drivers -path '*tests/test_*.c' -not -path '*/TegraGpu/*'); do
  d=$(dirname "$t"); run "$t" bash -c "cc -std=c99 -Wall -Wextra -Werror -I../src '$(basename "$t")' -o '$tmp/t' && '$tmp/t'"; done
python3 -I tools/check_l4t_reference.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_l4t_reference.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_l4t_reference.py"; tail -5 "$tmp/o"; }
# Erista GPU: ACPI node vs resource header (always) and full model tests (needs a pinned mesa-switch
# checkout: MESA_DIR=/path/to/mesa-switch @0ff52079, otherwise skipped).
python3 -I tools/check_acpi_resources.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_acpi_resources.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_acpi_resources.py"; tail -5 "$tmp/o"; }
if [ -n "${MESA_DIR:-}" ]; then
  python3 -I drivers/t210-icosa/drivers/TegraGpu/tools/build.py --mesa "$MESA_DIR" --out "$tmp/gpu" --host-only >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS TegraGpu build.py --host-only"; } || { fail=$((fail+1)); echo "FAIL TegraGpu build.py"; tail -8 "$tmp/o"; }
else echo "SKIP TegraGpu model tests (set MESA_DIR to a mesa-switch checkout)"; fi
echo; echo "passed $pass, failed $fail"; [ "$fail" -eq 0 ]
