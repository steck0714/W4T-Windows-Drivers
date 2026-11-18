/* W4T-CREDIT-BEGIN
 * Origin : hayato41200/Windows-for-Switch-Drivers, Erista (t210/Icosa) driver set, which matched an older
 *          ruje0504-2/Windows-for-Switch-Drivers commit (docs/HAYATO-DRIVER-AUDIT-ja.md); later edits are in CREDITS.md.
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : see CREDITS.md (per-source table).
 * Credit block added by W4T (2026-11-18); full table in CREDITS.md.
 * W4T-CREDIT-END */
/*
 * Support symbols that the MSVC toolchain normally provides and that
 * WdfDriverEntry.lib references when linked with LLVM lld-link.
 *
 * _guard_check_icall: Control Flow Guard check called before indirect calls.
 * These drivers are not built with /guard:cf, so it must do nothing. It is
 * naked so that it preserves every register (the ARM64 CFG thunk contract:
 * target in x15, argument registers untouched).
 */
__attribute__((naked)) void _guard_check_icall(void)
{
    __asm__ volatile("ret");
}
