/* SPDX-License-Identifier: GPL-2.0-only
 * Passive-level Windows resource/PMIC adapter for the DFLL core.
 * Caller holds CpuLock. Only the existing SwitchFan SPB target accesses M4.
 */
#ifndef SWITCHFAN_DFLL_WINDOWS_H
#define SWITCHFAN_DFLL_WINDOWS_H
#define SW_DFLL_GATE_BIT (1u << 27) /* shared DFLL_REF/SOC peripheral 155 */
#define CPU_ON_DFLL 0x2000ffffu

static ULONG SwDfllReadMap(PSWFAN_DEVICE d, ULONG map, ULONG offset)
{ return READ_REGISTER_ULONG((PULONG)(d->Dfll.Map[map] + offset)); }
static VOID SwDfllWriteMap(PSWFAN_DEVICE d, ULONG map, ULONG offset, ULONG value)
{
    WRITE_REGISTER_ULONG((PULONG)(d->Dfll.Map[map] + offset), value);
    KeMemoryBarrier();
}
static uint32_t SwDfllMmioRead(void *p, uint32_t offset)
{ return SwDfllReadMap(p, SwDfllMmio, offset); }
static void SwDfllMmioWrite(void *p, uint32_t offset, uint32_t value)
{ SwDfllWriteMap(p, SwDfllMmio, offset, value); }
static void SwDfllWait(void *p, uint32_t us)
{
    LARGE_INTEGER wait;
    (void)p;
    if (us < 1000u) KeStallExecutionProcessor(us);
    else { wait.QuadPart = -(LONGLONG)us * 10; KeDelayExecutionThread(KernelMode, FALSE, &wait); }
}
static bool SwDfllCpuVoltage(void *p, uint32_t *out)
{
    PSWFAN_DEVICE d = p;
    UCHAR code = 0;
    SwFanPmicLock(d);
    NTSTATUS status = SwFanReadReg(d->Pmic, CPU_M4_VOUT, &code);
    SwFanPmicUnlock(d);
    if (!NT_SUCCESS(status)) return false;
    *out = SwDfllCodeUv(code);
    return true;
}
static uint32_t SwDfllCpuParent(void *p)
{
    ULONG policy = SwCpuRead(p, CPU_CCLK_POLICY);
    return policy == CPU_ON_PLLX ? 1u : policy == CPU_ON_DFLL ? 2u : 0u;
}
static bool SwDfllBootPllx(void *p)
{
    PSWFAN_DEVICE d = p;
    ULONG old, next;
    if (SwDfllCpuParent(d) != 2u) return false;
    old = SwCpuRead(d, CPU_PLLX_BASE);
    next = (old & ~(CPU_PLL_DIVS | CPU_PLL_LOCK | CPU_PLL_BYPASS)) | (d->BootPllx & CPU_PLL_DIVS);
    /* CPU remains on DFLL; never park it on a disabled/relocking PLLX. */
    SwCpuWrite(d, CPU_PLLX_MISC, SwCpuRead(d, CPU_PLLX_MISC) | (1u << 18));
    SwCpuWrite(d, CPU_PLLX_BASE, next & ~CPU_PLL_ENABLE);
    KeStallExecutionProcessor(10u);
    SwCpuWrite(d, CPU_PLLX_BASE, next | CPU_PLL_ENABLE);
    for (ULONG i = 0; i < 1000u; ++i) {
        ULONG live = SwCpuRead(d, CPU_PLLX_BASE);
        if ((live & (CPU_PLL_ENABLE | CPU_PLL_LOCK | CPU_PLL_DIVS)) ==
            ((next | CPU_PLL_ENABLE | CPU_PLL_LOCK) & (CPU_PLL_ENABLE | CPU_PLL_LOCK | CPU_PLL_DIVS))) return true;
        KeStallExecutionProcessor(10u);
    }
    return false;
}
static bool SwDfllSelectCpu(void *p, bool dfll)
{
    PSWFAN_DEVICE d = p;
    ULONG policy = dfll ? CPU_ON_DFLL : CPU_ON_PLLX;
    if (!dfll &&
        (SwCpuRead(d, CPU_PLLX_BASE) & (CPU_PLL_ENABLE | CPU_PLL_LOCK | CPU_PLL_DIVS)) !=
        (CPU_PLL_ENABLE | CPU_PLL_LOCK | (d->BootPllx & CPU_PLL_DIVS))) return false;
    SwCpuWrite(d, CPU_CCLK_POLICY, policy);
    KeMemoryBarrier(); KeStallExecutionProcessor(2u);
    return SwCpuRead(d, CPU_CCLK_POLICY) == policy;
}
static struct sw_dfll_io SwFanDfllIo(PSWFAN_DEVICE d)
{
    return (struct sw_dfll_io){d, SwDfllMmioRead, SwDfllMmioWrite, SwDfllWait,
        SwDfllCpuVoltage, SwDfllCpuParent, SwDfllBootPllx, SwDfllSelectCpu};
}

static BOOLEAN SwFanDfllAvailable(PSWFAN_DEVICE d)
{
    LITE_DFLL_HANDOFF *h = &d->Dfll.Handoff;
    if (!d->IsLite || !d->CpuConfigured || !d->Car || !d->Pmic || !d->Config.RamActualKhz ||
        h->Magic != LITE_DFLL_HANDOFF_MAGIC || h->Version != LITE_DFLL_HANDOFF_VERSION ||
        h->Size != sizeof(*h) || (h->Hidrev & 0xfff0u) != 0x2120u || h->Sku != 0x83u ||
        !h->CpuSpeedo || h->CpuSpeedo > 5000u || h->CpuProcessId > 1u ||
        h->CpuProcessId != (h->CpuSpeedo < 1650u ? 0u : 1u) ||
        h->PmicAddress != d->Config.PmicAddress ||
        (h->ClockMKhz != 9600u && h->ClockMKhz != 12800u &&
         h->ClockMKhz != 19200u && h->ClockMKhz != 38400u)) return FALSE;
    for (ULONG i = 0; i < SwDfllResourceCount; ++i) if (!d->Dfll.Map[i]) return FALSE;
    return TRUE;
}

static BOOLEAN SwFanDfllReleaseClocks(PSWFAN_DEVICE d)
{
    SWFAN_DFLL *f = &d->Dfll;
    if (!f->ClocksOwned) return TRUE;
    if (SwDfllCpuParent(d) != 1u || f->Control.active || f->Control.prepared || f->Control.fault) return FALSE;
    SwDfllWriteMap(d, SwDfllReset, 0, f->SavedReset);
    SwDfllWriteMap(d, SwDfllSources, 0, f->SavedSources[0]);
    SwDfllWriteMap(d, SwDfllSources, 4, f->SavedSources[1]);
    if (f->SavedBankReset & SW_DFLL_GATE_BIT) SwDfllWriteMap(d, SwDfllBankReset, 0, SW_DFLL_GATE_BIT);
    if (!(f->SavedGate & SW_DFLL_GATE_BIT)) SwDfllWriteMap(d, SwDfllBankEnable, 4, SW_DFLL_GATE_BIT);
    f->ClocksOwned = FALSE;
    return TRUE;
}

static BOOLEAN SwFanDfllAcquireClocks(PSWFAN_DEVICE d, ULONG *i2c_khz)
{
    SWFAN_DFLL *f = &d->Dfll;
    ULONG pllp, m, n, rate, i2c, parent;
    if (!SwFanDfllAvailable(d) || f->ClocksOwned || SwDfllCpuParent(d) != 1u) return FALSE;
    /* Match Switchroot's fixed 408 MHz PLLP parent, without reprogramming it
     * or the shared I2C5 source. DFLL_REF/SOC use u7.1 divider 14 => 51 MHz.
     * I2C5 uses an integer divider, unlike the DFLL ref/soc source divider. */
    pllp = SwCpuRead(d, 0xa0u); m = pllp & 0x1fu; n = (pllp >> 8) & 0x3ffu;
    SwFanTrace(L"DfllPllpBase", pllp);
    SwFanTrace(L"DfllClockMKhz", f->Handoff.ClockMKhz);
    if ((pllp & ((1u << 31) | (1u << 30) | (1u << 27))) !=
        ((1u << 30) | (1u << 27))) return FALSE;
    /* Switchroot clk_pll_recalc_rate: with OVERRIDE clear PLLP uses its
     * fixed 408 MHz hardware configuration, not the software M/N fields.
     * With OVERRIDE set, OUT0 is the VCO; BASE.DIVP divides OUT2 only. */
    rate = !(pllp & (1u << 28)) ? 408000u : m ? (38400u * n) / m : 0u;
    if (rate != 408000u) return FALSE;
    i2c = SwDfllReadMap(d, SwDfllI2cSource, 0);
    SwFanTrace(L"DfllI2cSource", i2c);
    /* Switchroot commonly selects PLLP_OUT0 (0); WinX/Hekate selects
     * CLK_M (6). Preserve the shared I2C5 mux/divider and derive its rate. */
    switch ((i2c >> 29) & 7u) {
    case 0u: parent = rate; break;
    case 6u: parent = f->Handoff.ClockMKhz; break;
    default: return FALSE;
    }
    *i2c_khz = parent / ((i2c & 0xffffu) + 1u);
    SwFanTrace(L"DfllI2cKhz", *i2c_khz);
    f->SavedSources[0] = SwDfllReadMap(d, SwDfllSources, 0);
    f->SavedSources[1] = SwDfllReadMap(d, SwDfllSources, 4);
    f->SavedReset = SwDfllReadMap(d, SwDfllReset, 0);
    f->SavedBankReset = SwDfllReadMap(d, SwDfllBankStatus, 0);
    f->SavedGate = SwDfllReadMap(d, SwDfllBankStatus, 8);
    SwFanTrace(L"DfllAcquireStage", 1u);
    SwFanTrace(L"DfllSavedReset", f->SavedReset);
    SwFanTrace(L"DfllSavedBankReset", f->SavedBankReset);
    SwFanTrace(L"DfllSavedGate", f->SavedGate);
    if ((f->SavedGate & SW_DFLL_GATE_BIT) && !(f->SavedBankReset & SW_DFLL_GATE_BIT)) {
        ULONG control = SwDfllReadMap(d, SwDfllMmio, SW_DFLL_CTRL);
        ULONG output = SwDfllReadMap(d, SwDfllMmio, SW_DFLL_OUTPUT_CFG);
        ULONG force = SwDfllReadMap(d, SwDfllMmio, SW_DFLL_FORCE);
        SwFanTrace(L"DfllExistingControl", control);
        SwFanTrace(L"DfllExistingOutput", output);
        SwFanTrace(L"DfllExistingForce", force);
        /* WinX leaves CTRL disabled and DVCO reset, but OUTPUT_CFG retains
         * I2C_ENABLE. That bit alone is not a running controller. Never take
         * over an active loop or forced voltage output. CPU is still PLLX. */
        if ((control & 3u) || (force & 0x40u) ||
            ((output & SW_DFLL_I2C_ENABLE) && !(f->SavedReset & 1u))) {
            SwFanTrace(L"DfllAcquireStage", 2u); return FALSE;
        }
        if (output & SW_DFLL_I2C_ENABLE) {
            BOOLEAN idle = FALSE;
            SwDfllWriteMap(d, SwDfllMmio, SW_DFLL_OUTPUT_CFG, output & ~SW_DFLL_I2C_ENABLE);
            if (!(SwDfllReadMap(d, SwDfllMmio, SW_DFLL_OUTPUT_CFG) & SW_DFLL_I2C_ENABLE)) {
                /* Same two-read/2us minimum as Switchroot's pending check. */
                for (ULONG i = 0; i < 100u; ++i) {
                    ULONG pending = SwDfllReadMap(d, SwDfllMmio, SW_DFLL_I2C_STS);
                    KeStallExecutionProcessor(10u);
                    if (!(pending & 1u) && !(SwDfllReadMap(d, SwDfllMmio, SW_DFLL_I2C_STS) & 1u)) {
                        idle = TRUE; break;
                    }
                }
            }
            if (!idle) { SwFanTrace(L"DfllAcquireStage", 5u); return FALSE; }
        }
    }
    f->ClocksOwned = TRUE;
    /* Reset before ungating: stale I2C state must never issue an M4 write. */
    SwDfllWriteMap(d, SwDfllBankReset, 0, SW_DFLL_GATE_BIT);
    SwDfllWriteMap(d, SwDfllReset, 0, f->SavedReset | 1u);
    SwDfllWriteMap(d, SwDfllSources, 0, (f->SavedSources[0] & ~0xc00000ffu) | 14u);
    SwDfllWriteMap(d, SwDfllSources, 4, (f->SavedSources[1] & ~0xc00000ffu) | 14u);
    SwDfllWriteMap(d, SwDfllBankEnable, 0, SW_DFLL_GATE_BIT);
    KeStallExecutionProcessor(5u);
    SwDfllWriteMap(d, SwDfllBankReset, 4, SW_DFLL_GATE_BIT);
    SwDfllWriteMap(d, SwDfllReset, 0, f->SavedReset & ~1u);
    KeStallExecutionProcessor(5u);
    if ((SwDfllReadMap(d, SwDfllSources, 0) & 0xc00000ffu) != 14u ||
        (SwDfllReadMap(d, SwDfllSources, 4) & 0xc00000ffu) != 14u ||
        !(SwDfllReadMap(d, SwDfllBankStatus, 8) & SW_DFLL_GATE_BIT) ||
        (SwDfllReadMap(d, SwDfllBankStatus, 0) & SW_DFLL_GATE_BIT) ||
        (SwDfllReadMap(d, SwDfllReset, 0) & 1u)) {
        SwFanTrace(L"DfllAcquireStage", 3u);
        SwFanTrace(L"DfllRefReadback", SwDfllReadMap(d, SwDfllSources, 0));
        SwFanTrace(L"DfllSocReadback", SwDfllReadMap(d, SwDfllSources, 4));
        SwFanTrace(L"DfllGateReadback", SwDfllReadMap(d, SwDfllBankStatus, 8));
        SwFanTrace(L"DfllBankResetReadback", SwDfllReadMap(d, SwDfllBankStatus, 0));
        SwFanTrace(L"DfllDvcoResetReadback", SwDfllReadMap(d, SwDfllReset, 0));
        (void)SwFanDfllReleaseClocks(d); return FALSE;
    }
    SwFanTrace(L"DfllAcquireStage", 4u);
    return TRUE;
}

static NTSTATUS SwFanDfllStopLocked(PSWFAN_DEVICE d)
{
    struct sw_dfll_io io = SwFanDfllIo(d);
    if (d->Dfll.Control.prepared && !SwDfllStop(&io, &d->Dfll.Control))
        return d->Dfll.LastStatus = STATUS_DEVICE_HARDWARE_ERROR;
    if (!SwFanDfllReleaseClocks(d)) return d->Dfll.LastStatus = STATUS_DEVICE_HARDWARE_ERROR;
    if (SwDfllCpuParent(d) == 1u && SwCpuKhz(d) == d->BootCpuKhz) {
        d->CpuApplied = FALSE;
        SwFanEmcCpuRateChanged(d,d->BootCpuKhz);
    }
    return d->Dfll.LastStatus = STATUS_SUCCESS;
}

static ULONG SwFanDfllChooseRate(PSWFAN_DEVICE d)
{
    SWFAN_DFLL *f = &d->Dfll;
    ULONG load = f->LoadPermille, requested;
    ULONGLONG now = KeQueryInterruptTime();
    /* Missing user-mode utilization never invents idle time. Keep the
     * requested profile ceiling; thermal limits remain kernel-enforced. */
    if (!f->LoadTime || now - f->LoadTime > 30000000ULL) load = 1000;
    if (load > 1000u) load = 1000u;
    requested = f->CpuMinKhz + (ULONG)(((ULONGLONG)(f->CpuMaxKhz - f->CpuMinKhz) * load) / 1000u);
    if (load >= 800u) return f->CpuMaxKhz;
    if (requested <= SW_DFLL_MIN_KHZ) return SW_DFLL_MIN_KHZ;
    for (ULONG i = 0; i < SW_DFLL_OPP_COUNT; ++i)
        if (f->Control.table.opp[i].khz >= requested)
            return f->Control.table.opp[i].khz < f->CpuMaxKhz ? f->Control.table.opp[i].khz : f->CpuMaxKhz;
    return f->CpuMaxKhz;
}

static NTSTATUS SwFanDfllApplyProfileLocked(PSWFAN_DEVICE d, ULONG mode)
{
    SWFAN_DFLL *f = &d->Dfll;
    const struct sw_nvpmodel_profile *profile;
    struct sw_dfll_io io = SwFanDfllIo(d);
    ULONG i2c_khz, target;
    if (mode >= SwNvpCount || !SwFanDfllAvailable(d)) return STATUS_NOT_SUPPORTED;
    if (d->Stopping || !d->TempValid || d->LastTemp >= 96u || f->Control.fault || f->FaultLatched)
        return STATUS_DEVICE_NOT_READY;
    profile = &sw_nvpmodel_profiles[mode];
    f->CpuMinKhz = profile->cpu_min_khz ? profile->cpu_min_khz : SW_DFLL_MIN_KHZ;
    /* The original Console config requests 1963000; captured Linux resolves
     * this to the real 1963500 OPP. OC's 3000000 caps at the SLT table maximum. */
    f->CpuRequestedMaxKhz = profile->cpu_max_khz;
    f->CpuMaxKhz = profile->cpu_max_khz == 1963000u ? 1963500u :
        profile->cpu_max_khz > 2397000u ? 2397000u : profile->cpu_max_khz;
    if (!f->Control.prepared) {
        if (!SwFanDfllAcquireClocks(d, &i2c_khz)) return STATUS_DEVICE_NOT_READY;
        if (!SwDfllPrepare(&io, &f->Control, f->Handoff.CpuSpeedo, f->Handoff.CpuProcessId,
            d->Config.RamActualKhz,
            d->Config.PmicAddress, i2c_khz, SwDfllCodeUv(d->BootVout))) {
            (void)SwFanDfllReleaseClocks(d); return STATUS_DEVICE_HARDWARE_ERROR;
        }
    }
    if (f->Control.emc_floor_uv != f->EmcFloorUv) {
        /* A different Vmin changes the calibrated DVCO/skipper denominator. */
        RtlZeroMemory(f->Control.floor_khz, sizeof(f->Control.floor_khz));
        RtlZeroMemory(f->Control.high_floor_khz, sizeof(f->Control.high_floor_khz));
        f->Control.emc_floor_uv = f->EmcFloorUv;
    }
    target = SwFanDfllChooseRate(d);
    if (f->Control.active ? !SwDfllSetRate(&io, &f->Control, target, (int32_t)d->LastTemp) :
                           !SwDfllStart(&io, &f->Control, target, (int32_t)d->LastTemp)) {
        (void)SwFanDfllStopLocked(d);
        return f->LastStatus = STATUS_DEVICE_HARDWARE_ERROR;
    }
    d->CpuApplied = FALSE; /* CpuApplied denotes the legacy PLLX overclock only. */
    d->CpuThrottled = f->Control.thermally_limited;
    /* Like tegra210-cpufreq's successful rate-change callback, update the
     * bandwidth floor after the CPU transition. BPMP work runs separately;
     * never acquire the EMC transaction lock while holding CpuLock. */
    SwFanEmcCpuRateChanged(d,f->Control.requested_khz);
    /* Read-only IOCTL reports the current hardware result. Do not write the
     * registry four times per second from utilization hints; the fan timer
     * already records its periodic CPU sample. */
    return f->LastStatus = STATUS_SUCCESS;
}

static ULONG SwFanDfllReadKhz(PSWFAN_DEVICE d)
{
    uint32_t rate = 0;
    struct sw_dfll_io io = SwFanDfllIo(d);
    if (d->Dfll.Control.active && SwDfllMonitor(&io, &rate)) {
        ULONG request = SwDfllMmioRead(d, SW_DFLL_FREQ_REQ);
        return (ULONG)(((ULONGLONG)rate * (((request >> 8) & 255u) + 1u)) / 256u);
    }
    return 0;
}
#endif
