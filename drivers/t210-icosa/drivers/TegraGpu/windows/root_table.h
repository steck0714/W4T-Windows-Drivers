/* SPDX-License-Identifier: MIT */
#ifndef TEGRA_GPU_ROOT_TABLE_H
#define TEGRA_GPU_ROOT_TABLE_H
#include "table_segment.h"
#include "../platform/t210_mmu.h"

/* D3DGPU_PHYSICAL_ADDRESS only has an explicit Padding member in newer WDK headers
 * (the layout is the same; 10.0.19041 leaves the 4 bytes implicit). build.py defines
 * TG_D3DGPU_PA_HAS_PADDING when the WDK header declares it; without it the check is
 * skipped because there is no member to read. */
#ifdef TG_D3DGPU_PA_HAS_PADDING
#define TG_PA_PADDING(a) ((a).Padding)
#else
#define TG_PA_PADDING(a) 0
#endif

typedef struct _TG_GPU_ROOT_TABLE {
    D3DGPU_PHYSICAL_ADDRESS Address;
    UINT Entries;
    SIZE_T Bytes;
    UINT64 DeviceAddress;
    UINT64 VaLimit;
    BOOLEAN Valid;
} TG_GPU_ROOT_TABLE;

/* Helpers for the WDDM two-level root-table DDIs. The adapter owns process
 * and context handle validation; these helpers do not register an adapter.
 * A root move is resolved before publishing the new binding. On failure the
 * context must remain unschedulable (Root.Valid is cleared), not reuse its
 * old binding after VidMm may have moved/freed the old allocation.
 */
SIZE_T TgGpuRootTableSize(DXGKARG_GETROOTPAGETABLESIZE *Args);
NTSTATUS TgGpuResolveRootTable(const TG_GPU_TABLE_SEGMENT *Segment,
    const DXGKARG_SETROOTPAGETABLE *Args, TG_GPU_ROOT_TABLE *Root);

/* Caller serializes this with submission and holds the context idle until
 * instance invalidation/rebind is complete. Instance is an owned uncached
 * private allocation; only RAMIN PDB/VA-limit words are changed here.
 */
NTSTATUS TgGpuWriteRootInstance(const TG_GPU_ROOT_TABLE *Root,
    const TG_GPU_PRIVATE_MEMORY *Instance, BOOLEAN ContextIdle);

/* Execute at the paging fence's ordered submission point, not while merely
 * building a future paging buffer. Keep old table/data pages resident until
 * this returns success or a successful hardware drain/reset retires them.
 * Caller owns GPU power and serializes the invalidate registers.
 */
NTSTATUS TgGpuFlushRootTable(const TG_GPU_ROOT_TABLE *Root,
    const DXGK_BUILDPAGINGBUFFER_FLUSHTLB *Args,
    const struct tg_gpu_power_io *Io, const struct tg_gpu_power *Power);
#endif
