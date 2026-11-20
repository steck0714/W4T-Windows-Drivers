/* Minimal stand-in for the EDK II headers, ONLY for `cc -fsyntax-only` in uefi/tests/gpu_handoff/run.sh.
 * It is not a build environment: declarations here are just enough to catch typos and type errors in
 * the .c files under uefi/edk2 on a machine without EDK II. The real build is the winx tree. */
#ifndef UEFI_SHIM_H
#define UEFI_SHIM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32; typedef uint64_t UINT64;
typedef uintptr_t UINTN; typedef uint16_t CHAR16; typedef char CHAR8; typedef bool BOOLEAN; typedef void VOID;
typedef UINTN EFI_STATUS; typedef void *EFI_HANDLE; typedef void *EFI_EVENT;
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static
#define EFIAPI
#define TRUE true
#define FALSE false
#define NULL_GUARD 0
#define EFI_SUCCESS 0
#define EFI_ERROR(s) ((s) != 0)
#define EVT_SIGNAL_EXIT_BOOT_SERVICES 0x201
#define TPL_NOTIFY 16
#define L_(x) x
typedef struct { UINT32 a; UINT16 b, c; UINT8 d[8]; } EFI_GUID;
typedef void (EFIAPI *EFI_EVENT_NOTIFY)(EFI_EVENT, VOID *);
typedef struct {
  EFI_STATUS (*Stall)(UINTN);
  EFI_STATUS (*CreateEvent)(UINT32, UINTN, EFI_EVENT_NOTIFY, VOID *, EFI_EVENT *);
  EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID *, VOID **);
} EFI_BOOT_SERVICES;
typedef struct { EFI_BOOT_SERVICES *BootServices; } EFI_SYSTEM_TABLE;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_GUID gEfiShellParametersProtocolGuid;
UINTN Print(const CHAR16 *Format, ...);
UINT32 MmioRead32(UINTN Address);
UINT32 MmioWrite32(UINTN Address, UINT32 Value);
void MemoryFence(void);
int StrCmp(const CHAR16 *a, const CHAR16 *b);
#endif
