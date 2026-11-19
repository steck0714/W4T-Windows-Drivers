/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2016-2020 NVIDIA CORPORATION.
 * Copyright (c) 2021 CTCaer.
 * T210 MC_ALL sampling, adapted from cactmon_t21x.c / actmon_common.c at
 * switch-l4t-kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b and
 * tegra210-soc-actmon.dtsi, l4t-soc-t210 0d7816046cb06b637a3b70381a5e4994fd897c35.
 * Clock mux: clk-tegra-periph.c, kernel-4.9 2d0059fd3167a8df756de2aa0489d4aa70a9fc15.
 * This owns only the inactive MC_ALL monitor. No EMC clock/timing writes.
 */
#ifndef SW_T214_ACTMON_H
#define SW_T214_ACTMON_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum sw_actmon_map { SwActmonRegisters, SwActmonSource, SwActmonReset,
                      SwActmonEnable, SwActmonResetStatus, SwActmonGateStatus,
                      SwActmonMapCount };
/* clk.c periph_regs[119 / 32] is bank V, bit 23. Keep the actual register
 * ranges beside the sampler so Windows mapping and address controls share
 * this declaration. Gate status is within the existing DFLL read-only CRS. */
static const struct { uint32_t address, length; bool read_only; }
SwActmonResources[SwActmonMapCount] = {
    {0x6000c800u,0x400u,false}, {0x600063e8u,4u,false},
    {0x60006430u,8u,false}, {0x60006440u,8u,false},
    {0x60006358u,4u,true}, {0x60006360u,4u,true}
};
enum { SwActmonOk=0, SwActmonWaiting=1, SwActmonInvalid=-1,
       SwActmonBusy=-2, SwActmonReadback=-3 };
#define SW_ACTMON_GATE (1u << 23) /* CAR clock/reset 119, bank V */
#define SW_ACTMON_PERIOD 20u
#define SW_ACTMON_MC_ALL 0x1c0u
#define SW_ACTMON_ENABLE (1u << 31)
#define SW_ACTMON_CONTROL (SW_ACTMON_ENABLE | (1u << 18) | (6u << 10))
#define SW_ACTMON_SOURCE_MASK 0xc00000ffu
#define SW_ACTMON_CLK_M 0xc0000000u /* mux3, u7.1 divisor=0 => CLK_M */

struct sw_actmon_io {
    void *context;
    uint32_t (*read)(void *, unsigned map, uint32_t offset);
    void (*write)(void *, unsigned map, uint32_t offset, uint32_t value);
    void (*delay_us)(void *, uint32_t);
};
struct sw_actmon {
    uint32_t saved_source, saved_reset, saved_gate, saved_control, saved_weight;
    uint64_t ready_ms, last_sample_ms;
    bool owned, clock_ready, borrowed_clock, registers_modified;
};
static inline bool sw_actmon_write(const struct sw_actmon_io *io, unsigned map,
                                   uint32_t offset, uint32_t value, uint32_t mask)
{
    io->write(io->context,map,offset,value);
    return (io->read(io->context,map,offset)&mask)==(value&mask);
}
static inline int sw_actmon_start(struct sw_actmon *s, const struct sw_actmon_io *io,
                                  uint32_t clk_m_khz, uint32_t emc_khz, uint64_t now_ms)
{
    uint32_t source, reset, gate, control;
    if (!s || !io || !io->read || !io->write || !io->delay_us || s->owned ||
        clk_m_khz!=19200u || !emc_khz || emc_khz>3200000u) return SwActmonInvalid;
    source=io->read(io->context,SwActmonSource,0);
    reset=io->read(io->context,SwActmonResetStatus,0);
    gate=io->read(io->context,SwActmonGateStatus,0);
    if (source==UINT32_MAX || reset==UINT32_MAX || gate==UINT32_MAX) return SwActmonReadback;
    memset(s,0,sizeof(*s));
    s->saved_source=source; s->saved_reset=reset; s->saved_gate=gate;
    s->borrowed_clock=(gate&SW_ACTMON_GATE) && !(reset&SW_ACTMON_GATE);
    if (s->borrowed_clock) {
        /* The clock may feed another monitor. Keep its source and global
         * period untouched; refuse an already enabled MC_ALL owner. */
        control=io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL);
        if ((source&SW_ACTMON_SOURCE_MASK)!=SW_ACTMON_CLK_M ||
            io->read(io->context,SwActmonRegisters,4)!=SW_ACTMON_PERIOD-1u ||
            control==UINT32_MAX || (control&SW_ACTMON_ENABLE)) return SwActmonBusy;
        s->clock_ready=true;
    }
    s->owned=true;
    if (!s->borrowed_clock) {
        io->write(io->context,SwActmonReset,0,SW_ACTMON_GATE);
        if (!(io->read(io->context,SwActmonResetStatus,0)&SW_ACTMON_GATE)) return SwActmonReadback;
        if (!sw_actmon_write(io,SwActmonSource,0,
            (source&~SW_ACTMON_SOURCE_MASK)|SW_ACTMON_CLK_M,SW_ACTMON_SOURCE_MASK)) return SwActmonReadback;
        io->write(io->context,SwActmonEnable,0,SW_ACTMON_GATE);
        io->delay_us(io->context,5);
        io->write(io->context,SwActmonReset,4,SW_ACTMON_GATE);
        io->delay_us(io->context,5);
        if ((io->read(io->context,SwActmonResetStatus,0)&SW_ACTMON_GATE) ||
            !(io->read(io->context,SwActmonGateStatus,0)&SW_ACTMON_GATE)) return SwActmonReadback;
        s->clock_ready=true;
        if (!sw_actmon_write(io,SwActmonRegisters,4,SW_ACTMON_PERIOD-1u,UINT32_MAX)) return SwActmonReadback;
    }
    s->saved_control=io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL);
    s->saved_weight=io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x18u);
    if (s->saved_control==UINT32_MAX || s->saved_weight==UINT32_MAX) return SwActmonReadback;
    s->registers_modified=true;
    if (!sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL,0,UINT32_MAX) ||
        !sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL+0x18u,0x400u,UINT32_MAX)) return SwActmonReadback;
    io->write(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x0cu,emc_khz*SW_ACTMON_PERIOD);
    io->write(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x24u,UINT32_MAX);
    /* Polling path: watermark interrupts remain disabled. */
    if (!sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL,SW_ACTMON_CONTROL,UINT32_MAX)) return SwActmonReadback;
    s->ready_ms=now_ms+SW_ACTMON_PERIOD; s->last_sample_ms=now_ms;
    return SwActmonOk;
}
static inline int sw_actmon_sample(struct sw_actmon *s, const struct sw_actmon_io *io,
                                   uint32_t maximum_khz, uint64_t now_ms,
                                   uint32_t *average_khz, uint32_t *raw_khz)
{
    uint32_t average, raw;
    *average_khz=*raw_khz=0;
    if (!s->owned || !s->clock_ready || !maximum_khz || maximum_khz>3200000u) return SwActmonInvalid;
    if (now_ms<s->ready_ms || now_ms-s->last_sample_ms<SW_ACTMON_PERIOD) return SwActmonWaiting;
    if (io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL)!=SW_ACTMON_CONTROL ||
        (io->read(io->context,SwActmonSource,0)&SW_ACTMON_SOURCE_MASK)!=SW_ACTMON_CLK_M ||
        (io->read(io->context,SwActmonResetStatus,0)&SW_ACTMON_GATE) ||
        !(io->read(io->context,SwActmonGateStatus,0)&SW_ACTMON_GATE)) return SwActmonReadback;
    average=io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x20u);
    raw=io->read(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x1cu);
    if (average>maximum_khz*SW_ACTMON_PERIOD || raw>maximum_khz*SW_ACTMON_PERIOD) return SwActmonReadback;
    *average_khz=average/SW_ACTMON_PERIOD; *raw_khz=raw/SW_ACTMON_PERIOD;
    s->last_sample_ms=now_ms;
    return SwActmonOk;
}
static inline bool sw_actmon_stop(struct sw_actmon *s, const struct sw_actmon_io *io)
{
    bool ok=true;
    if (!s->owned) return true;
    if (s->clock_ready && s->registers_modified) {
        ok=sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL,0,UINT32_MAX);
        io->write(io->context,SwActmonRegisters,SW_ACTMON_MC_ALL+0x24u,UINT32_MAX);
        if (s->borrowed_clock) {
            ok=sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL+0x18u,s->saved_weight,UINT32_MAX)&&ok;
            ok=sw_actmon_write(io,SwActmonRegisters,SW_ACTMON_MC_ALL,s->saved_control,UINT32_MAX)&&ok;
        }
    }
    if (!s->borrowed_clock) {
        io->write(io->context,SwActmonReset,0,SW_ACTMON_GATE);
        ok=sw_actmon_write(io,SwActmonSource,0,s->saved_source,SW_ACTMON_SOURCE_MASK)&&ok;
        if (!(s->saved_reset&SW_ACTMON_GATE)) io->write(io->context,SwActmonReset,4,SW_ACTMON_GATE);
        if (!(s->saved_gate&SW_ACTMON_GATE)) io->write(io->context,SwActmonEnable,4,SW_ACTMON_GATE);
        ok=((io->read(io->context,SwActmonResetStatus,0)^s->saved_reset)&SW_ACTMON_GATE)==0 && ok;
        ok=((io->read(io->context,SwActmonGateStatus,0)^s->saved_gate)&SW_ACTMON_GATE)==0 && ok;
    }
    if (ok) memset(s,0,sizeof(*s));
    return ok;
}
#endif
