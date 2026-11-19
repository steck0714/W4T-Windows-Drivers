/* SPDX-License-Identifier: GPL-2.0-only
 * W4T build shim, force-included (clang-cl /FI) into selected WDM driver sources by tools/Build-Driver.ps1
 * (-ForceInclude, set with "force_include" in tools/ci/drivers-*.json).
 *
 * ZwFlushBuffersFile is declared in ntifs.h, not in ntddk.h. The windows-switch sources (swhid.c / swbatt.c /
 * touchread.c) include only ntddk.h, and newer clang (16+) treats an implicit function declaration as an error
 * (-Wimplicit-function-declaration), so the call does not compile. The sources are pinned by SHA-256 against upstream
 * (tools/aula_layout.py --check), so the declaration is supplied here instead of editing them.
 * The prototype is identical to the one in ntifs.h; a repeated identical declaration is legal C.
 */
#pragma once
#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif
NTSYSAPI NTSTATUS NTAPI ZwFlushBuffersFile(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock);
#ifdef __cplusplus
}
#endif
