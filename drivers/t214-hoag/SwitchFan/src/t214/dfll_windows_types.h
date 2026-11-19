/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SWITCHFAN_DFLL_WINDOWS_TYPES_H
#define SWITCHFAN_DFLL_WINDOWS_TYPES_H
#include "dfll_control.h"
#include "dfll_handoff.h"
#include "nvpmodel_profiles.h"

enum { SwDfllMmio, SwDfllSources, SwDfllReset, SwDfllBankStatus,
       SwDfllBankReset, SwDfllBankEnable, SwDfllI2cSource, SwDfllResourceCount };
static const struct {
    ULONG Address, Length;
    BOOLEAN ReadOnly;
} SwDfllResources[SwDfllResourceCount] = {
    {0x70110000u,0x300u,FALSE}, {0x6000662cu,8u,FALSE}, {0x600062f4u,4u,FALSE},
    {0x6000635cu,12u,TRUE}, {0x60006438u,8u,FALSE}, {0x60006448u,8u,FALSE},
    {0x60006128u,4u,TRUE},
};
typedef struct {
    volatile UCHAR *Map[SwDfllResourceCount];
    LITE_DFLL_HANDOFF Handoff;
    struct sw_dfll_control Control;
    ULONG SavedSources[2], SavedReset, SavedGate, SavedBankReset;
    ULONG CpuMinKhz, CpuMaxKhz, CpuRequestedMaxKhz, LoadPermille;
    ULONG EmcFloorUv;
    ULONGLONG LoadTime;
    NTSTATUS LastStatus;
    BOOLEAN ClocksOwned, FaultLatched;
} SWFAN_DFLL;
#endif
