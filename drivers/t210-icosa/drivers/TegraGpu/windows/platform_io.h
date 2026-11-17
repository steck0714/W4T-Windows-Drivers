/* SPDX-License-Identifier: MIT */
#ifndef TG_GPU_PLATFORM_IO_H
#define TG_GPU_PLATFORM_IO_H
#include "resources.h"
#include "pmic.h"
#include "../platform/t210_power.h"
typedef struct _TG_GPU_PLATFORM {
    const TG_GPU_RESOURCES *Resources; /* borrowed through hardware retirement */
    HANDLE Bridge;      /* SPB target 0x1C (MAX77621 GPU); nonzero = platform open */
    HANDLE Pmic620;     /* SPB target 0x3C (MAX77620, shared) */
    NTSTATUS IoStatus;
} TG_GPU_PLATFORM;

/* PASSIVE, serialized lifetime; mappings remain owned by the caller.
 * Opens the two I2C5 SPB targets from the resource list's connection IDs.
 */
NTSTATUS TgGpuOpenPlatform(TG_GPU_PLATFORM *,const TG_GPU_RESOURCES *);
NTSTATUS TgGpuClosePlatform(TG_GPU_PLATFORM *);
NTSTATUS TgGpuPlatformRequest(TG_GPU_PLATFORM *,struct tg_gpu_pmic_request *);
NTSTATUS TgGpuPlatformPreflight(TG_GPU_PLATFORM *,UCHAR *PmicAddress);
/* Mapped-register reads/writes can run in the ISR. PMIC/PLLP queries and
 * delays that sleep are PASSIVE worker operations, as in the diagnostic.
 */
struct tg_gpu_power_io TgGpuPlatformIo(TG_GPU_PLATFORM *);
#endif
