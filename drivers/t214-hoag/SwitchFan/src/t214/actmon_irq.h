/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2016-2020 NVIDIA CORPORATION.
 * Copyright (c) 2021 CTCaer.
 * MC_ALL watermark handling adapted from cactmon_t21x.c / actmon_common.c,
 * switch-l4t-kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b.
 * Thresholds/windows: tegra210-soc-actmon.dtsi, l4t-soc-t210
 * 0d7816046cb06b637a3b70381a5e4994fd897c35. Live Noble overrides are separate.
 *
 * The caller serializes ALL functions with the interrupt lock. No allocation,
 * sleeping, BPMP command, PMIC transaction, or frequency change belongs here.
 */
#ifndef SW_T214_ACTMON_IRQ_H
#define SW_T214_ACTMON_IRQ_H
#include "actmon.h"

#define SW_ACTMON_GLOBAL_MC_ALL (1u << 26)
#define SW_ACTMON_IRQ_UP        (1u << 31)
#define SW_ACTMON_IRQ_DOWN      (1u << 30)
#define SW_ACTMON_IRQ_AVG_DOWN  (1u << 25)
#define SW_ACTMON_IRQ_AVG_UP    (1u << 24)
#define SW_ACTMON_IRQ_STATUS    (SW_ACTMON_IRQ_UP | SW_ACTMON_IRQ_DOWN | \
                                SW_ACTMON_IRQ_AVG_DOWN | SW_ACTMON_IRQ_AVG_UP)
#define SW_ACTMON_IRQ_UP_EN     (1u << 30)
#define SW_ACTMON_IRQ_DOWN_EN   (1u << 29)
#define SW_ACTMON_IRQ_AVG_EN    ((1u << 21) | (1u << 20))
#define SW_ACTMON_IRQ_ENABLES   (SW_ACTMON_IRQ_UP_EN | SW_ACTMON_IRQ_DOWN_EN | SW_ACTMON_IRQ_AVG_EN)
#define SW_ACTMON_IRQ_CONTROL   (SW_ACTMON_CONTROL | (2u << 23) | SW_ACTMON_IRQ_ENABLES)

struct sw_actmon_irq {
    uint32_t current_khz, maximum_khz, boost_khz;
    uint32_t average_count, raw_count, control, saved_marks[4];
    uint64_t interrupts;
    bool marks_saved, running;
    int error;
};

static inline bool sw_actmon_irq_write(const struct sw_actmon_io *io,
                                      uint32_t offset, uint32_t value)
{
    return sw_actmon_write(io, SwActmonRegisters, SW_ACTMON_MC_ALL + offset, value, UINT32_MAX);
}

static inline void sw_actmon_irq_mask(struct sw_actmon_irq *s, const struct sw_actmon_io *io)
{
    /* Preserve the sampling engine until the passive owner finishes its stop.
     * Mask only this monitor; never clear another monitor's interrupt status. */
    if (!s->marks_saved) return;
    s->control &= ~SW_ACTMON_IRQ_ENABLES;
    if (!sw_actmon_irq_write(io, 0, s->control)) s->error = SwActmonReadback;
    io->write(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x24u, UINT32_MAX);
    s->running = false;
}

static inline int sw_actmon_irq_fail(struct sw_actmon_irq *s, const struct sw_actmon_io *io)
{
    s->error = SwActmonReadback;
    sw_actmon_irq_mask(s, io);
    return s->error;
}

static inline bool sw_actmon_irq_average_marks(struct sw_actmon_irq *s,
                                              const struct sw_actmon_io *io)
{
    /* Linux ACTMON_DEFAULT_AVG_BAND = 6/1000 of the actual maximum. */
    uint32_t band = (s->maximum_khz * 6u / 1000u) * SW_ACTMON_PERIOD;
    uint32_t low = s->average_count > band ? s->average_count - band : 0u;
    return sw_actmon_irq_write(io, 0x10u, s->average_count + band) &&
           sw_actmon_irq_write(io, 0x14u, low);
}

static inline int sw_actmon_irq_rate(struct sw_actmon_irq *s, const struct sw_actmon_io *io,
                                     uint32_t actual_khz)
{
    if (!s->marks_saved || !actual_khz || actual_khz > s->maximum_khz || s->error)
        return SwActmonInvalid;
    /* Called only after GET_RATE confirms the transition. Raw thresholds track
     * that clock; neither requested frequency nor old-OPP percentages qualify. */
    uint32_t count = actual_khz * SW_ACTMON_PERIOD;
    if (!sw_actmon_irq_write(io, 4u, count * 60u / 100u) ||
        !sw_actmon_irq_write(io, 8u, count * 40u / 100u)) return sw_actmon_irq_fail(s, io);
    s->current_khz = actual_khz;
    return SwActmonOk;
}

static inline int sw_actmon_irq_start(struct sw_actmon_irq *s, const struct sw_actmon *base,
                                      const struct sw_actmon_io *io, uint32_t actual_khz,
                                      uint32_t maximum_khz)
{
    const uint32_t offsets[] = {4u, 8u, 0x10u, 0x14u};
    if (!s || !base || !base->owned || !base->clock_ready || s->marks_saved ||
        !actual_khz || actual_khz > maximum_khz || maximum_khz > 3200000u)
        return SwActmonInvalid;
    for (unsigned i = 0; i < 4u; ++i) {
        s->saved_marks[i] = io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + offsets[i]);
        if (s->saved_marks[i] == UINT32_MAX) return SwActmonReadback;
    }
    s->marks_saved = true;
    s->maximum_khz = maximum_khz;
    s->average_count = actual_khz * SW_ACTMON_PERIOD;
    s->control = SW_ACTMON_IRQ_CONTROL;
    if (sw_actmon_irq_rate(s, io, actual_khz) || !sw_actmon_irq_average_marks(s, io))
        return sw_actmon_irq_fail(s, io);
    io->write(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x24u, UINT32_MAX);
    if (!sw_actmon_irq_write(io, 0, s->control)) return sw_actmon_irq_fail(s, io);
    s->running = true;
    return SwActmonOk;
}

static inline int sw_actmon_irq_counts(struct sw_actmon_irq *s, const struct sw_actmon_io *io)
{
    if (!s->running || s->error) return s->error ? s->error : SwActmonInvalid;
    uint32_t avg = io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x20u);
    uint32_t raw = io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x1cu);
    if (avg > s->maximum_khz * SW_ACTMON_PERIOD || raw > s->maximum_khz * SW_ACTMON_PERIOD ||
        io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL) != s->control)
        return sw_actmon_irq_fail(s, io);
    s->average_count = avg; s->raw_count = raw;
    return SwActmonOk;
}

/* Return false for another monitor's IRQ. True means ours was acknowledged,
 * including a latched readback fault that the passive owner must publish. */
static inline bool sw_actmon_irq_service(struct sw_actmon_irq *s, const struct sw_actmon_io *io)
{
    uint32_t status, global;
    if (!s->running) return false;
    global = io->read(io->context, SwActmonRegisters, 0);
    if (!(global & SW_ACTMON_GLOBAL_MC_ALL)) return false;
    if (global == UINT32_MAX) { sw_actmon_irq_fail(s, io); return true; }
    status = io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x24u);
    if (!status) return false;
    if ((status & ~SW_ACTMON_IRQ_STATUS) || sw_actmon_irq_counts(s, io)) {
        sw_actmon_irq_fail(s, io); return true;
    }
    if (!sw_actmon_irq_average_marks(s, io)) { sw_actmon_irq_fail(s, io); return true; }
    if (status & SW_ACTMON_IRQ_UP) {
        uint64_t boost = (uint64_t)s->boost_khz * 2u + 16000u;
        s->boost_khz = boost < s->maximum_khz ? (uint32_t)boost : s->maximum_khz;
        s->control |= SW_ACTMON_IRQ_UP_EN | SW_ACTMON_IRQ_DOWN_EN;
        if (s->boost_khz == s->maximum_khz) s->control &= ~SW_ACTMON_IRQ_UP_EN;
    } else if (status & SW_ACTMON_IRQ_DOWN) {
        /* Hardware already counted three low windows. No additional software
         * three-poll delay or 300-ms downclock holdoff is applied. */
        s->boost_khz /= 2u;
        s->control |= SW_ACTMON_IRQ_UP_EN | SW_ACTMON_IRQ_DOWN_EN;
        if (!s->boost_khz) s->control &= ~SW_ACTMON_IRQ_DOWN_EN;
    }
    if (!sw_actmon_irq_write(io, 0, s->control)) { sw_actmon_irq_fail(s, io); return true; }
    io->write(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL + 0x24u, UINT32_MAX);
    ++s->interrupts;
    return true;
}

static inline bool sw_actmon_irq_restore(struct sw_actmon_irq *s, const struct sw_actmon_io *io)
{
    const uint32_t offsets[] = {4u, 8u, 0x10u, 0x14u};
    if (!s->marks_saved) return true;
    sw_actmon_irq_mask(s, io);
    bool ok = true;
    for (unsigned i = 0; i < 4u; ++i)
        ok = sw_actmon_irq_write(io, offsets[i], s->saved_marks[i]) && ok;
    /* The caller next restores the base control/clock. A previously latched
     * sampling fault does not imply that restoration itself failed. */
    return ok && io->read(io->context, SwActmonRegisters, SW_ACTMON_MC_ALL) == s->control;
}
#endif
