/* SPDX-License-Identifier: GPL-2.0-only
 * Portable T210B01 DFLL control core. The platform owns CAR, CPU parent
 * switching and MAX77812/SPB; all entry points require its CPU transaction lock.
 * Register/loop semantics: NVIDIA clk-dfll.c, Switchroot kernel
 * 2d0059fd3167a8df756de2aa0489d4aa70a9fc15.
 * Copyright (C) 2012-2018 NVIDIA Corporation. All rights reserved.
 * Copyright (c) 2021-2023, CTCaer
 */
#ifndef SWITCHFAN_T214_DFLL_CONTROL_H
#define SWITCHFAN_T214_DFLL_CONTROL_H
#include "dfll_tables.h"

/* Offsets are relative to the ACPI-owned 0x70110000 aperture. */
#define SW_DFLL_CTRL       0x00u
#define SW_DFLL_CONFIG     0x04u
#define SW_DFLL_PARAMS     0x08u
#define SW_DFLL_TUNE0      0x0cu
#define SW_DFLL_TUNE1      0x10u
#define SW_DFLL_FREQ_REQ   0x14u
#define SW_DFLL_DROOP      0x1cu
#define SW_DFLL_OUTPUT_CFG 0x20u
#define SW_DFLL_FORCE      0x24u
#define SW_DFLL_MON_CTRL   0x28u
#define SW_DFLL_MON_DATA   0x2cu
#define SW_DFLL_I2C_CFG    0x40u
#define SW_DFLL_I2C_REG    0x44u
#define SW_DFLL_I2C_STS    0x48u
#define SW_DFLL_INTR_STS   0x5cu
#define SW_DFLL_INTR_EN    0x60u
#define SW_DFLL_I2C_DIV    0x16cu /* integrated controller base + 0x6c */
#define SW_DFLL_LUT        0x200u
#define SW_DFLL_I2C_ENABLE (1u << 30)

struct sw_dfll_io {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*delay_us)(void *, uint32_t);
    bool (*read_cpu_uv)(void *, uint32_t *);
    /* 1=PLLX, 2=DFLL, 0=unverified/other. Read actual CPU mux ownership. */
    uint32_t (*cpu_parent)(void *);
    /* Relock and verify the boot PLLX while CPU still runs on DFLL. */
    bool (*prepare_boot_pllx)(void *);
    /* Neither callback may lower the rail or disable the running CPU source. */
    bool (*select_cpu_parent)(void *, bool dfll);
};
struct sw_dfll_control {
    struct sw_dfll_table table;
    struct sw_dfll_request request;
    uint32_t boot_uv, requested_khz, measured_khz, measured_uv;
    uint32_t emc_floor_uv;
    uint32_t floor_khz[3], high_floor_khz[3];
    uint32_t high_threshold_khz, high_default_floor_khz;
    uint8_t lut_min, lut_max, lut_safe, high_min, high_start;
    bool prepared, active, high_tune, fault, thermally_limited;
};

static bool SwDfllWrite(const struct sw_dfll_io *io, uint32_t reg, uint32_t value, uint32_t mask)
{
    io->write(io->context, reg, value);
    return (io->read(io->context, reg) & mask) == (value & mask);
}

static uint32_t SwDfllRateBelowUv(const struct sw_dfll_table *t, uint32_t uv)
{
    uint32_t rate = t->opp[0].khz;
    for (uint32_t i = 0; i < SW_DFLL_OPP_COUNT && t->opp[i].uv <= uv; ++i) rate = t->opp[i].khz;
    return rate;
}

static uint32_t SwDfllRateAboveUv(const struct sw_dfll_table *t, uint32_t uv)
{
    for (uint32_t i = 0; i < SW_DFLL_OPP_COUNT; ++i)
        if (t->opp[i].uv > uv) return t->opp[i].khz;
    return t->opp[SW_DFLL_OPP_COUNT - 1u].khz;
}

static bool SwDfllLimits(const struct sw_dfll_io *io, struct sw_dfll_control *s,
                         uint8_t minimum, uint8_t maximum)
{
    uint32_t value = io->read(io->context, SW_DFLL_OUTPUT_CFG);
    if (value == UINT32_MAX || minimum >= s->table.lut_size ||
        maximum >= s->table.lut_size || minimum > maximum) return false;
    value = (value & ~0x003f3f00u) | ((uint32_t)minimum << 8) | ((uint32_t)maximum << 16);
    if (!SwDfllWrite(io, SW_DFLL_OUTPUT_CFG, value, 0x7f3f3fffu)) return false;
    s->lut_min = minimum; s->lut_max = maximum;
    return true;
}

static bool SwDfllTune(const struct sw_dfll_io *io, struct sw_dfll_control *s, bool high)
{
    if (!SwDfllWrite(io, SW_DFLL_TUNE0, high ? s->table.tune0_high : s->table.tune0_low, UINT32_MAX) ||
        !SwDfllWrite(io, SW_DFLL_TUNE1, s->table.tune1, UINT32_MAX)) return false;
    s->high_tune = high;
    return true;
}

static bool SwDfllMonitor(const struct sw_dfll_io *io, uint32_t *dvco_khz)
{
    uint32_t a, b;
    if (!SwDfllWrite(io, SW_DFLL_MON_CTRL, 6u, 7u)) return false;
    /* Synchronize to DATA_NEW; its count measures DVCO before the skipper. */
    (void)io->read(io->context, SW_DFLL_MON_DATA);
    for (uint32_t n = 0; n < 100u; ++n) {
        io->delay_us(io->context, 10u);
        a = io->read(io->context, SW_DFLL_MON_DATA);
        if (a == UINT32_MAX) return false;
        if (!(a & (1u << 16))) continue;
        a &= 0xffffu;
        b = io->read(io->context, SW_DFLL_MON_DATA) & 0xffffu;
        if (!a || !b || (a > b ? a - b : b - a) > 2u) continue;
        *dvco_khz = b * (SW_DFLL_REF_KHZ / 4u);
        return true;
    }
    return false;
}

static bool SwDfllDelivered(const struct sw_dfll_io *io, const struct sw_dfll_control *s,
                            uint8_t minimum, bool exact)
{
    for (uint32_t n = 0; n < 100u; ++n) {
        uint32_t v = io->read(io->context, SW_DFLL_I2C_STS), uv = 0;
        uint32_t index = (v >> 1) & 63u;
        /* Tegra requires two clear PENDING samples separated by >=2 us. */
        if (v == UINT32_MAX || (v & 1u)) { io->delay_us(io->context, 20u); continue; }
        io->delay_us(io->context, 2u);
        v = io->read(io->context, SW_DFLL_I2C_STS);
        index = (v >> 1) & 63u;
        if (v != UINT32_MAX && !(v & 1u) && index < s->table.lut_size &&
            (exact ? index == minimum : index >= minimum) &&
            io->read_cpu_uv(io->context, &uv) &&
            uv >= SwDfllCodeUv(s->table.lut[minimum])) {
            io->delay_us(io->context, 500u);
            return true;
        }
        io->delay_us(io->context, 20u);
    }
    return false;
}

static bool SwDfllWriteRequest(const struct sw_dfll_io *io, struct sw_dfll_control *s,
                               const struct sw_dfll_request *r, uint8_t force_min)
{
    int32_t index = r->lut_index > force_min ? r->lut_index : force_min;
    int32_t force = (index - (int32_t)s->lut_safe) * 64; /* 128 / cg=2 */
    uint32_t value;
    if (force < -2048) force = -2048;
    if (force > 2047) force = 2047;
    value = r->mult | ((uint32_t)r->scale << 8) | (1u << 7) | (1u << 28) |
            (((uint32_t)force & 0xfffu) << 16);
    if (!SwDfllWrite(io, SW_DFLL_FREQ_REQ, value, 0x1fffffffu)) return false;
    s->request = *r;
    return true;
}

static bool SwDfllPrepare(const struct sw_dfll_io *io, struct sw_dfll_control *s,
                          uint32_t speedo, uint32_t process, uint32_t emc_khz,
                          uint32_t pmic_address, uint32_t i2c_clock_khz, uint32_t boot_uv)
{
    uint32_t divisor, cold, cap;
    if (!io || !s || !io->read || !io->write || !io->delay_us || !io->read_cpu_uv ||
        !io->select_cpu_parent || !io->cpu_parent || !io->prepare_boot_pllx || s->active ||
        io->cpu_parent(io->context) != 1u || (pmic_address != 0x31u && pmic_address != 0x33u) ||
        i2c_clock_khz < 3200u || boot_uv < 600000u || boot_uv > SW_DFLL_MAX_UV) return false;
    memset(s, 0, sizeof(*s));
    if (!SwDfllBuildTable(speedo, process, emc_khz, &s->table)) return false;
    s->boot_uv = boot_uv;
    if (boot_uv > SwDfllCodeUv(s->table.lut[s->table.lut_size - 1u])) return false;
    cold = SwDfllLutCeil(&s->table, 800000u);
    cap = SwDfllLutFloor(&s->table, 1010000u);
    s->lut_safe = (uint8_t)(cold + (cold < cap));
    s->high_min = SwDfllLutCeil(&s->table, 850000u);
    s->high_start = SwDfllLutCeil(&s->table, 888000u);
    s->high_threshold_khz = SwDfllRateAboveUv(&s->table, SwDfllCodeUv(s->table.lut[s->high_min]));
    s->high_default_floor_khz = SwDfllRateAboveUv(&s->table, SwDfllCodeUv(s->table.lut[s->high_start]));
    divisor = (i2c_clock_khz + 3199u) / 3200u;
    if (!divisor || divisor > 65536u) return false;
    /* Platform verifies CPU is on PLLX before calling; this never changes M4. */
    if (!SwDfllWrite(io, SW_DFLL_CTRL, 0u, 3u) ||
        !SwDfllWrite(io, SW_DFLL_OUTPUT_CFG, (uint32_t)s->lut_safe << 24, 0x7f3f3fffu) ||
        !SwDfllWrite(io, SW_DFLL_FORCE, 0u, 0x7fu) ||
        !SwDfllWrite(io, SW_DFLL_CONFIG, 128u, 0xffu) ||
        !SwDfllWrite(io, SW_DFLL_PARAMS, 0x00460002u, 0x01ffffffu) ||
        !SwDfllTune(io, s, false) ||
        !SwDfllWrite(io, SW_DFLL_DROOP, 0x00000f00u, UINT32_MAX) ||
        !SwDfllWrite(io, SW_DFLL_INTR_EN, 0u, 3u)) return false;
    io->write(io->context, SW_DFLL_INTR_STS, 3u);
    for (uint32_t i = 0; i < SW_DFLL_LUT_SIZE; ++i) {
        uint32_t j = i < s->table.lut_size ? i : s->table.lut_size - 1u;
        if (!SwDfllWrite(io, SW_DFLL_LUT + i * 4u, s->table.lut[j], 0xffu)) return false;
    }
    if (!SwDfllLimits(io, s, (uint8_t)cold, (uint8_t)cap) ||
        !SwDfllWrite(io, SW_DFLL_I2C_CFG, 0x00107000u | (pmic_address << 1), 0x001fffffu) ||
        !SwDfllWrite(io, SW_DFLL_I2C_REG, 0x26u, 0xffu) ||
        !SwDfllWrite(io, SW_DFLL_I2C_DIV, ((divisor - 1u) << 16) | 1u, UINT32_MAX)) return false;
    s->prepared = true;
    return true;
}

static bool SwDfllSetRate(const struct sw_dfll_io *io, struct sw_dfll_control *s,
                          uint32_t requested_khz, int32_t temperature_c)
{
    struct sw_dfll_request req;
    uint32_t range, floor_uv, cap_uv, target, floor_khz, dvco, uv;
    uint8_t low, high;
    bool want_high;
    if (!s->prepared || !s->active || s->fault || temperature_c < -40 || temperature_c > 120)
        return false;
    range = temperature_c <= 20 ? 0u : temperature_c <= 70 ? 1u : 2u;
    floor_uv = range == 0u ? 800000u : SwDfllCodeUv(s->table.lut[0]);
    if (floor_uv < s->emc_floor_uv) floor_uv = s->emc_floor_uv;
    cap_uv = temperature_c >= 84 ? 1010000u : temperature_c >= 64 ? 1060000u : SW_DFLL_MAX_UV;
    high = SwDfllLutFloor(&s->table, cap_uv);
    target = SwDfllRateBelowUv(&s->table, SwDfllCodeUv(s->table.lut[high]));
    if (target > requested_khz) target = requested_khz;
    if (target < SW_DFLL_MIN_KHZ) return false;
    s->thermally_limited = target != requested_khz;
    want_high = target >= s->high_threshold_khz;
    low = SwDfllLutCeil(&s->table, floor_uv);
    floor_khz = s->floor_khz[range] ? s->floor_khz[range] :
        SwDfllRateBelowUv(&s->table, SwDfllCodeUv(s->table.lut[low]));
    if (want_high) {
        uint32_t hf = s->high_floor_khz[range] ? s->high_floor_khz[range] : s->high_default_floor_khz;
        if (floor_khz < hf) floor_khz = hf;
        if (low < s->high_min) low = s->high_min;
    } else if (s->high_tune && !SwDfllTune(io, s, false)) goto fail;
    if (high < low + 2u) goto fail;
    floor_khz = ((floor_khz + 25499u) / 25500u) * 25500u;
    if (!SwDfllRateRequest(&s->table, target, floor_khz, &req) ||
        !SwDfllLimits(io, s, low, high) ||
        !SwDfllWriteRequest(io, s, &req, want_high ? s->high_start : low)) goto fail;
    if (want_high && !s->high_tune) {
        if (!SwDfllDelivered(io, s, s->high_min, false)) goto fail;
        io->delay_us(io->context, 2000u);
        if (!SwDfllTune(io, s, true)) goto fail;
    }
    /* Hardware changes voltage and DVCO together in closed loop. A command
     * write is not completion: require fresh frequency and real M4 readback. */
    for (uint32_t attempt = 0; attempt < 40u; ++attempt) {
        uint32_t measured, difference, tolerance, status;
        io->delay_us(io->context, 100u);
        if (!SwDfllMonitor(io, &dvco) || !io->read_cpu_uv(io->context, &uv)) goto fail;
        measured = (uint32_t)(((uint64_t)dvco * ((uint32_t)req.scale + 1u)) / 256u);
        difference = measured > req.scaled_khz ? measured - req.scaled_khz : req.scaled_khz - measured;
        tolerance = (SW_DFLL_REF_KHZ / 2u) * ((uint32_t)req.scale + 1u) / 256u;
        if (difference <= tolerance && uv >= SwDfllCodeUv(s->table.lut[low]) &&
            uv <= SwDfllCodeUv(s->table.lut[high])) {
            s->requested_khz = target; s->measured_khz = measured; s->measured_uv = uv;
            return true;
        }
        /* At Vmin a faster-than-requested DVCO needs a calibrated skipper
         * denominator. Derive it from this chip's fresh monitor, never from
         * another unit's recorded 943.5 MHz floor. */
        status = io->read(io->context, SW_DFLL_I2C_STS);
        if (!(status & 1u) && ((status >> 1) & 63u) == low &&
            uv == SwDfllCodeUv(s->table.lut[low]) && dvco > req.dvco_khz && dvco <= 3238500u) {
            floor_khz = ((dvco + 25499u) / 25500u) * 25500u;
            if (want_high) s->high_floor_khz[range] = floor_khz;
            else s->floor_khz[range] = floor_khz;
            if (!SwDfllRateRequest(&s->table, target, floor_khz, &req) ||
                !SwDfllWriteRequest(io, s, &req, low)) goto fail;
        }
    }
fail:
    s->fault = true;
    return false;
}

static bool SwDfllStop(const struct sw_dfll_io *io, struct sw_dfll_control *s)
{
    uint8_t boot;
    uint32_t output;
    if (!s->prepared) return !s->active;
    if (s->active) {
        /* Low tuning is characterized at every supported rail. Hold a known
         * voltage for the boot PLLX before handing CPU/M4 control back. */
        boot = SwDfllLutCeil(&s->table, s->boot_uv > s->emc_floor_uv ? s->boot_uv : s->emc_floor_uv);
        if (io->cpu_parent(io->context) != 2u || !io->prepare_boot_pllx(io->context) ||
            !SwDfllTune(io, s, false) ||
            !SwDfllWrite(io, SW_DFLL_FORCE, 0x40u | boot, 0x7fu)) {
            s->fault = true; return false;
        }
        output = io->read(io->context, SW_DFLL_OUTPUT_CFG) | SW_DFLL_I2C_ENABLE;
        if (!SwDfllWrite(io, SW_DFLL_OUTPUT_CFG, output, 0x7f3f3fffu) ||
            !SwDfllDelivered(io, s, boot, true)) { s->fault = true; return false; }
        (void)io->select_cpu_parent(io->context, false);
        if (io->cpu_parent(io->context) != 1u) { s->fault = true; return false; }
        s->active = false;
    }
    output = io->read(io->context, SW_DFLL_OUTPUT_CFG) & ~SW_DFLL_I2C_ENABLE;
    if (!SwDfllWrite(io, SW_DFLL_CTRL, 1u, 3u) ||
        !SwDfllWrite(io, SW_DFLL_OUTPUT_CFG, output, 0x7f3f3fffu)) goto fail;
    for (uint32_t n = 0; n < 100u; ++n) {
        io->delay_us(io->context, 2u);
        if (!(io->read(io->context, SW_DFLL_I2C_STS) & 1u)) {
            io->delay_us(io->context, 2u);
            if (io->read(io->context, SW_DFLL_I2C_STS) & 1u) continue;
            if (!SwDfllWrite(io, SW_DFLL_CTRL, 0u, 3u) ||
                !SwDfllWrite(io, SW_DFLL_FORCE, 0u, 0x7fu)) goto fail;
            s->prepared = false; s->fault = false;
            return true;
        }
    }
fail:
    s->fault = true;
    return false;
}

static bool SwDfllStart(const struct sw_dfll_io *io, struct sw_dfll_control *s,
                        uint32_t target_khz, int32_t temperature_c)
{
    struct sw_dfll_request start;
    uint32_t uv = 0, output;
    if (!s->prepared || s->active || s->fault || io->cpu_parent(io->context) != 1u ||
        !io->read_cpu_uv(io->context, &uv) ||
        uv < s->boot_uv || uv > SW_DFLL_MAX_UV) return false;
    if (!SwDfllRateRequest(&s->table, 1020000u, 1020000u, &start) ||
        !SwDfllWriteRequest(io, s, &start, s->lut_min) ||
        /* Match upstream open-loop: low tuning, no skipper or force feedforward. */
        !SwDfllWrite(io, SW_DFLL_FREQ_REQ,
                     (io->read(io->context, SW_DFLL_FREQ_REQ) | 0xff00u) & ~(1u << 28),
                     0x1fffffffu) ||
        !SwDfllWrite(io, SW_DFLL_CTRL, 1u, 3u)) return false;
    io->delay_us(io->context, 1u);
    {
        bool selected = io->select_cpu_parent(io->context, true);
        uint32_t parent = io->cpu_parent(io->context);
        s->active = parent != 1u;
        if (!selected || parent != 2u) {
            s->fault = true;
            if (parent == 1u || parent == 2u) (void)SwDfllStop(io, s);
            return false;
        }
    }
    output = io->read(io->context, SW_DFLL_OUTPUT_CFG) | SW_DFLL_I2C_ENABLE;
    if (!SwDfllWrite(io, SW_DFLL_OUTPUT_CFG, output, 0x7f3f3fffu) ||
        !SwDfllWrite(io, SW_DFLL_CTRL, 2u, 3u) ||
        !SwDfllSetRate(io, s, target_khz, temperature_c)) {
        (void)SwDfllStop(io, s);
        return false;
    }
    return true;
}
#endif
