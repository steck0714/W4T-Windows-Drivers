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
python3 -I tools/check_abi_sync.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_abi_sync.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_abi_sync.py"; tail -8 "$tmp/o"; }
python3 -I tools/check_acpi_resources.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_acpi_resources.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_acpi_resources.py"; tail -5 "$tmp/o"; }
python3 -I tools/check_no_proprietary.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_no_proprietary.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_no_proprietary.py"; tail -8 "$tmp/o"; }
python3 -I tools/check_aula_audit.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_aula_audit.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_aula_audit.py"; tail -8 "$tmp/o"; }
python3 -I tools/aula_layout.py --check >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/aula_layout.py --check"; } || { fail=$((fail+1)); echo "FAIL tools/aula_layout.py --check"; tail -8 "$tmp/o"; }
python3 -I tools/check_credits.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/check_credits.py"; } || { fail=$((fail+1)); echo "FAIL tools/check_credits.py"; tail -8 "$tmp/o"; }
# fwfetch (Rust firmware collector): needs cargo + mke2fs, otherwise exit 77 = skipped
python3 tools/fwfetch/tests/run_tests.py >"$tmp/o" 2>&1; rc=$?
if [ $rc -eq 0 ]; then pass=$((pass+1)); echo "PASS tools/fwfetch/tests/run_tests.py"; elif [ $rc -eq 77 ]; then echo "SKIP tools/fwfetch tests (need cargo and mke2fs)"; else fail=$((fail+1)); echo "FAIL tools/fwfetch/tests/run_tests.py"; grep FAIL "$tmp/o" | head -8; fi
python3 -I tools/check_dsdt_compile.py >"$tmp/o" 2>&1; rc=$?
if [ $rc -eq 0 ]; then pass=$((pass+1)); echo "PASS tools/check_dsdt_compile.py"; elif [ $rc -eq 77 ]; then echo "SKIP tools/check_dsdt_compile.py (need iasl: set IASL or put it on PATH)"; else fail=$((fail+1)); echo "FAIL tools/check_dsdt_compile.py"; tail -8 "$tmp/o"; fi
python3 -I tools/build/tests/test_build_tools.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/build/tests/test_build_tools.py"; } || { fail=$((fail+1)); echo "FAIL tools/build/tests/test_build_tools.py"; grep FAIL "$tmp/o" | head -8; }
python3 -I tools/ci/tests/test_fw_bundle.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/ci/tests/test_fw_bundle.py"; } || { fail=$((fail+1)); echo "FAIL tools/ci/tests/test_fw_bundle.py"; grep FAIL "$tmp/o" | head -8; }
python3 -I tools/ci/tests/test_verify_build.py >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS tools/ci/tests/test_verify_build.py"; } || { fail=$((fail+1)); echo "FAIL tools/ci/tests/test_verify_build.py"; grep -E "FAIL|Error" "$tmp/o" | head -8; }
if [ -n "${MESA_DIR:-}" ]; then
  python3 -I drivers/t210-icosa/drivers/TegraGpu/tools/build.py --mesa "$MESA_DIR" --out "$tmp/gpu" --host-only >"$tmp/o" 2>&1 && { pass=$((pass+1)); echo "PASS TegraGpu build.py --host-only"; } || { fail=$((fail+1)); echo "FAIL TegraGpu build.py"; tail -8 "$tmp/o"; }
else echo "SKIP TegraGpu model tests (set MESA_DIR to a mesa-switch checkout)"; fi
echo; echo "passed $pass, failed $fail"; [ "$fail" -eq 0 ]
