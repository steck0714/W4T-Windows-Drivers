/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2014-2018 NVIDIA CORPORATION. All rights reserved.
 * Copyright 2026 ruje0504-2 (structure, counter and restore logic from the Lite path)
 * Erista legacy GPCPLL sequence follows Switchroot nvgpu clk_gm20b.c
 * (clk_lock_gpc_pll_under_bypass, B1 branch): bypass -> iddq exit -> coeff
 * -> enable -> wait lock -> sync mode -> sel_vco. See NOTICE.
 * No live frequency scaling is implemented.
 */
#include "t210_clock.h"
#include "../core/identity.h"

#define GPU 0x57000000u
#define CFG 0x137000u
#define COEFF 0x137004u
#define VCO 0x137100u
#define OUT 0x137250u
#define BYPASS 0x137340u
#define SLOW 0x020160u
#define THERM 0x020798u
#define COUNT_CFG 0x134124u
#define COUNT_VAL 0x134128u
#define ENABLE 1u
#define IDDQ 2u
#define SYNC 4u
#define LOCK (1u << 17)
#define LOCKDET_OFF (1u << 4)   /* nvgpu enb_lckdet_power_off */
#define LOCK_TIMEOUT_US 501u    /* nvgpu: lock_timeout(500)+1 */
#define IDDQ_EXIT_US 5u

static const uint32_t clock_regs[TG_GPU_CLOCK_SAVED_REGS] = {
    CFG, COEFF, VCO, OUT, BYPASS, SLOW
};
/* Ignore read-only lock/calibration/testout bits when restoring control. */
static const uint32_t clock_masks[TG_GPU_CLOCK_SAVED_REGS] = {
    0x17, 0x3fffff, 1, 0x80003f3f, 1, 0x003f0000
};

static uint32_t clk_read(const struct tg_gpu_power_io *io, uint32_t reg)
{ return io->read32(io->context, GPU + reg); }

static void clk_write(const struct tg_gpu_power_io *io, uint32_t reg, uint32_t val)
{
    io->write32(io->context, GPU + reg, val);
    (void)clk_read(io, reg);
}

static void clk_update(const struct tg_gpu_power_io *io, uint32_t reg,
                       uint32_t mask, uint32_t val)
{ clk_write(io, reg, (clk_read(io, reg) & ~mask) | (val & mask)); }

static void clk_select(const struct tg_gpu_power_io *io, uint32_t vco)
{
    /* Switchroot masks throttling only across the mux update. */
    uint32_t saved = clk_read(io, THERM);
    clk_write(io, THERM, 0);
    clk_update(io, VCO, 1, vco);
    clk_write(io, THERM, saved);
}

/* Erista GM20B CVB table (legacy / speedo id 0), voltage = f(speedo, frequency).
 * Coefficients and formula: Linux nouveau drivers/gpu/drm/nouveau/nvkm/subdev/
 * volt/gm20b.c (gm20b_cvb_coef) and volt/gk20a.c (gk20a_volt_get_cvb_voltage /
 * gk20a_volt_calc_voltage), (c) 2014-2016 NVIDIA CORPORATION, MIT. Same
 * numbers as NVIDIA L4T tegra21_dvfs.c. Only the two profiles used here. */
struct cvb { uint32_t khz; int32_t c0, c1, c2; };
static const struct cvb cvb_table[] = {
    { TG_GPU_FIXED_KHZ,     2122872,  -95095, 1632 },
    { TG_GPU_OVERCLOCK_KHZ, 2458841, -102685, 1632 },
};
#define SPEEDO_FUSE   0x7000f930u  /* FUSE_CPU_SPEEDO_2 shadow: GPU speedo is fused here */
#define SPEEDO_SCALE  100
#define VMIN_UV       950000u      /* nouveau speedo_to_vmin[], highest entry (conservative) */
#define VMAX_UV       1150000u     /* regulator-max-microvolt of vdd-gpu (platform-t210-nx) */

static int32_t div_round_closest(int64_t n, int32_t d)
{
    return (int32_t)(n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d);
}

/* The raw fuse maps to the GPU speedo value differently per speedo revision
 * (Linux speedo-tegra210.c): raw (rev >= 3), (-1662 + 1082 * raw / 100) / 10
 * (rev 2) or raw - 75 (older). The revision is not read here. Voltage falls
 * as speedo rises, so the smallest candidate gives the highest voltage: safe. */
static int32_t gpu_speedo_floor(uint32_t raw)
{
    int32_t a = (int32_t)raw, b = (-1662 + (int32_t)(1082u * raw / 100u)) / 10, c = (int32_t)raw - 75;
    int32_t m = a < b ? a : b;
    return m < c ? m : c;
}

uint32_t tg_gpu_clock_required_uv(uint32_t raw_speedo, uint32_t target)
{
    uint32_t i, uv, code;
    int32_t s, mv;
    if (!raw_speedo || raw_speedo > 0xffffu) return UINT32_MAX;
    s = gpu_speedo_floor(raw_speedo);
    if (s <= 0) return UINT32_MAX;
    for (i = 0; i < sizeof(cvb_table) / sizeof(cvb_table[0]); ++i) {
        const struct cvb *e = &cvb_table[i];
        if (e->khz != target) continue;
        /* ((c2 * s / scale + c1) * s / scale + c0), then up to whole mV (nouveau). */
        mv = div_round_closest((int64_t)e->c2 * s, SPEEDO_SCALE);
        mv = div_round_closest((int64_t)(mv + e->c1) * s, SPEEDO_SCALE) + e->c0;
        if (mv <= 0) return UINT32_MAX;
        uv = (uint32_t)((mv + 999) / 1000) * 1000u;
        if (uv < VMIN_UV) uv = VMIN_UV;
        /* Round up onto the MAX77621 6.25 mV grid; refuse beyond the rail limit. */
        code = (uv - TG_GPU_MAX77621_BASE_UV + TG_GPU_MAX77621_STEP_UV - 1u) / TG_GPU_MAX77621_STEP_UV;
        uv = TG_GPU_MAX77621_BASE_UV + code * TG_GPU_MAX77621_STEP_UV;
        return uv > VMAX_UV ? UINT32_MAX : uv;
    }
    return UINT32_MAX;
}

static bool profile(uint32_t target, uint32_t *n, uint32_t *pl)
{
    if (target == TG_GPU_FIXED_KHZ) { *n = 48; *pl = 2; return true; }
    if (target == TG_GPU_OVERCLOCK_KHZ) { *n = 40; *pl = 1; return true; }
    return false;
}

enum tg_gpu_clock_result tg_gpu_clock_start(const struct tg_gpu_power_io *io,
    const struct tg_gpu_power *power, uint32_t target, struct tg_gpu_clock *c)
{
    uint32_t i, coeff, n, pl, required_uv;
    uint8_t code, readback;
    struct tg_gpu_identity id;
    if (!io || !power || !c || !io->read32 || !io->write32 ||
        !io->read_pmic || !io->write_pmic || !io->delay_us || !power->ready || c->owned ||
        !profile(target, &n, &pl) ||
        !tg_gpu_identify(clk_read(io, 0), &id) || id.chipset != TG_GPU_CHIP_GM20B)
        return TG_GPU_CLOCK_BAD_STATE;
    c->stage = 0;
    /* Freshly reset GPU only. Existing clock owners are never displaced. */
    if ((clk_read(io, CFG) & ENABLE) || (clk_read(io, VCO) & 1))
        return TG_GPU_CLOCK_BAD_STATE;
    /* Rail from this chip's own speed bin. An unreadable (0) or unsupported
     * bin, or one that needs more than the rail limit at this clock, is refused
     * before any PMIC write rather than run under-volted. */
    required_uv = tg_gpu_clock_required_uv(io->read32(io->context, SPEEDO_FUSE), target);
    if (required_uv == UINT32_MAX)
        return TG_GPU_CLOCK_RAIL_ERROR;
    /* PLL is still disabled; program voltage before frequency. power_down
     * restores the pre-acquisition PMIC state after clocks/reset are stopped.
     */
    code = tg_gpu_max77621_code(required_uv);
    if (!io->write_pmic(io->context, power->pmic_address, 0x00, (uint8_t)(code | 0x80u)) ||
        !io->write_pmic(io->context, power->pmic_address, 0x01, (uint8_t)(code | 0x80u)) ||
        !io->read_pmic(io->context, power->pmic_address, 0x00, &readback) ||
        (readback & 0x7fu) != code)
        return TG_GPU_CLOCK_RAIL_ERROR;
    io->delay_us(io->context, 5000);
    c->rail_uv = TG_GPU_MAX77621_BASE_UV + TG_GPU_MAX77621_STEP_UV * code;
    c->target_khz = target;
    for (i = 0; i < TG_GPU_CLOCK_SAVED_REGS; ++i)
        c->saved[i] = clk_read(io, clock_regs[i]);
    c->owned = true;
    c->running = false;
    c->stage = 1;

    clk_select(io, 0);                          /* put PLL in bypass first */
    clk_update(io, OUT, 0x80003f3f, 0x80000000);/* sdiv14 indiv4, vcodiv 1, bypdiv 1 */
    clk_update(io, BYPASS, 1, 0);               /* bypassctrl: GPCPLL VCO */
    /* Switchroot disables idle slowdown during clock setup. Keep it off for
     * the fixed profile and restore it with the other controls on stop. */
    clk_update(io, SLOW, 0x003f0000, 0);
    if (clk_read(io, SLOW) & 0x003f0000)
        return TG_GPU_CLOCK_READBACK_ERROR;
    c->stage = 2;

    if (clk_read(io, CFG) & IDDQ) {
        clk_update(io, CFG, IDDQ, 0);           /* first power-up: leave IDDQ */
        io->delay_us(io->context, IDDQ_EXIT_US);
    } else {
        clk_update(io, CFG, SYNC, 0);
        clk_update(io, CFG, ENABLE, 0);
    }
    c->stage = 3;
    /* M=1, N, PL. Under bypass the target is programmed directly: there is
     * no live consumer to protect, so the DVFS "slide" is not needed. */
    coeff = 1u | (n << 8) | (pl << 16);
    clk_update(io, COEFF, 0x3fffff, coeff);
    clk_update(io, CFG, ENABLE, ENABLE);
    if (clk_read(io, CFG) & LOCKDET_OFF)        /* nvgpu: power lock detector on */
        clk_update(io, CFG, LOCKDET_OFF, 0);
    for (i = 0; i < LOCK_TIMEOUT_US; ++i) {
        if (clk_read(io, CFG) & LOCK) break;
        io->delay_us(io->context, 1);
    }
    if (i == LOCK_TIMEOUT_US)
        return TG_GPU_CLOCK_CALIBRATION_ERROR;  /* PLL did not lock */
    clk_update(io, CFG, SYNC, SYNC);            /* glitchless switch out of bypass */
    clk_select(io, 1);
    if ((clk_read(io, COEFF) & 0x3fffff) != coeff ||
        (clk_read(io, CFG) & (ENABLE | SYNC | IDDQ)) != (ENABLE | SYNC) ||
        !(clk_read(io, VCO) & 1))
        return TG_GPU_CLOCK_READBACK_ERROR;
    c->stage = 4;
    c->running = true;
    return tg_gpu_clock_measure(io, c);
}

enum tg_gpu_clock_result tg_gpu_clock_measure(const struct tg_gpu_power_io *io,
    struct tg_gpu_clock *c)
{
    uint32_t slow, counter_cfg, first, second;
    if (!c->owned || !c->running) return TG_GPU_CLOCK_BAD_STATE;
    slow = clk_read(io, SLOW);
    counter_cfg = clk_read(io, COUNT_CFG);
    clk_update(io, SLOW, 0x3f0000, 0);
    clk_write(io, COUNT_CFG, 0x01000000);
    clk_write(io, COUNT_CFG, 0x00110000 | 800);
    io->delay_us(io->context, 200);
    first = clk_read(io, COUNT_VAL) & 0xfffff;
    io->delay_us(io->context, 100);
    second = clk_read(io, COUNT_VAL) & 0xfffff;
    clk_write(io, COUNT_CFG, counter_cfg);
    clk_write(io, SLOW, slow);
    c->counter = second;
    c->measured_khz = (uint32_t)((uint64_t)second * TG_GPU_REFERENCE_KHZ / 800);
    /* Counter resolution is 48 kHz; tolerate 0.5%. This is a live counter
     * check, not a report of programmed N. */
    if (!first || first != second || c->measured_khz < c->target_khz * 995u / 1000u ||
        c->measured_khz > c->target_khz * 1005u / 1000u)
        return TG_GPU_CLOCK_COUNTER_ERROR;
    c->stage = 5;
    return TG_GPU_CLOCK_OK;
}

enum tg_gpu_clock_result tg_gpu_clock_stop(const struct tg_gpu_power_io *io,
    struct tg_gpu_clock *c)
{
    uint32_t i;
    bool ok = true;
    if (!c->owned) return TG_GPU_CLOCK_OK;
    clk_select(io, 0);
    clk_update(io, CFG, SYNC, 0);
    clk_update(io, CFG, ENABLE, 0);
    c->running = false;
    /* Restore CFG last, so the original IDDQ state follows other registers. */
    for (i = TG_GPU_CLOCK_SAVED_REGS; i-- > 0; ) {
        clk_update(io, clock_regs[i], clock_masks[i], c->saved[i]);
        if ((clk_read(io, clock_regs[i]) & clock_masks[i]) !=
            (c->saved[i] & clock_masks[i])) ok = false;
    }
    if (!ok) return TG_GPU_CLOCK_READBACK_ERROR;
    c->owned = false;
    return TG_GPU_CLOCK_OK;
}
