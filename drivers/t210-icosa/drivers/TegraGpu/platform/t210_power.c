/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014-2019, NVIDIA CORPORATION. All rights reserved.
 * Copyright (C) 2010 Google, Inc.
 * Copyright 2026 ruje0504-2 (Lite power sequencing this file was adapted from)
 * Erista adaptation: GPU rail ownership follows CTCaer Hekate
 * bdk/power/max7762x.[ch] and max77620.h (GPL-2.0): MAX77621 GPU at I2C5
 * 0x1C, EN pin = MAX77620 GPIO6, voltage = 606.25 mV + 6.25 mV * code,
 * Horizon control values CONTROL1=0xB0 / CONTROL2=0xC1. Clock/reset/clamp
 * order follows Switchroot nvgpu platform_gk20a_tegra.c (GPL-2.0) and the
 * MC drain follows tegra_mc_flush().
 */
#include "t210_power.h"

#define CAR_BASE        0x60006000u
#define CAR_CLOCK_X     (CAR_BASE + 0x280u)
#define CAR_CLOCK_SET   (CAR_BASE + 0x284u)
#define CAR_CLOCK_CLR   (CAR_BASE + 0x288u)
#define CAR_RESET_X     (CAR_BASE + 0x28cu)
#define CAR_RESET_SET   (CAR_BASE + 0x290u)
#define CAR_RESET_CLR   (CAR_BASE + 0x294u)
#define CAR_PLLP_OUT5   (CAR_BASE + 0x67cu)
#define GPU_GATE        (1u << 24) /* Tegra clock/reset 184 */
#define GPU_REF         (1u << 29) /* Tegra clock 189 */
#define GPU_CLOCKS      (GPU_GATE | GPU_REF)
#define OUT5_MASK       0xff070000u
/* 408 MHz * 2 / (2 + 2) = 204 MHz with the fixed-divider override; keep
 * enable/reset. Same value the Lite path uses (Switchroot OUT5 handling).
 */
#define OUT5_204MHZ     0x02070000u
#define PMC_GPU_CLAMP   0x7000e6d4u
#define MC_FLUSH        0x70019970u
#define MC_FLUSH_STATUS 0x70019974u
#define MC_GPU_BIT      (1u << 2) /* MC client 34 in the second bank */
#define APB_MISC_HIDREV 0x70000804u
/* HIDREV [15:4]: chip id 0x21, major revision 1 = T210 (Hekate
 * GP_HIDREV_MAJOR_T210); major 2 is T210B01 (Mariko/Lite), refused here. */
#define HIDREV_T210_MASK  0xfff0u
#define HIDREV_T210_VALUE 0x2110u

/* MAX77620 (0x3C) */
#define M620_GPIO6      0x3Cu
#define M620_AME_GPIO   0x40u
#define GPIO_PUSHPULL   0x01u
#define GPIO_OUTPUT     0x00u
#define GPIO_VAL_HIGH   0x08u
#define GPIO6_ENABLE    (GPIO_VAL_HIGH | GPIO_OUTPUT | GPIO_PUSHPULL) /* 0x09 */
#define GPIO6_CFG_MASK  0x0bu  /* drv, dir, out value; bit2 is the input level */
#define AME_GPIO6       (1u << 6)

/* MAX77621 (0x1C) */
#define M621_VOUT       0x00u
#define M621_VOUT_DVS   0x01u
#define M621_CONTROL1   0x02u
#define M621_CONTROL2   0x03u
#define M621_ENABLE_BIT 0x80u
#define M621_VOLT_MASK  0x7fu
/* Hekate MAX77621_CPU_CTRL{1,2}_HOS_DEFAULT: AD|NFSR|SNS|12mV/us and
 * T_JUNCTION_120|WDTMR|CKKADV 75mV/us|NOMINAL inductor. */
#define M621_CTRL1_HOS  0xb0u
#define M621_CTRL2_HOS  0xc1u
#define GPU_BOOT_UV     800000u

static void update32(const struct tg_gpu_power_io *io, uint32_t address,
                     uint32_t mask, uint32_t value)
{
    uint32_t old = io->read32(io->context, address);
    io->write32(io->context, address, (old & ~mask) | (value & mask));
    (void)io->read32(io->context, address);
}

/* Write then read back under a mask (the GPIO input level and the VOUT
 * enable bit are not guaranteed to read back as written). */
static bool write_pmic_checked(const struct tg_gpu_power_io *io, uint8_t address,
                               uint8_t reg, uint8_t value, uint8_t mask)
{
    uint8_t readback;
    return io->write_pmic(io->context, address, reg, value) &&
           io->read_pmic(io->context, address, reg, &readback) &&
           (readback & mask) == (value & mask);
}

static bool set_rail_voltage(const struct tg_gpu_power_io *io, uint8_t addr,
                             uint8_t code, bool enable_bit)
{
    uint8_t v = (uint8_t)(code | (enable_bit ? M621_ENABLE_BIT : 0));
    /* Hekate max7762x_regulator_set_voltage(): VOUT, then VOUT_DVS with the
     * enable bit set, so the DVS selection tracks the voltage. */
    return write_pmic_checked(io, addr, M621_VOUT, v, M621_VOLT_MASK) &&
           write_pmic_checked(io, addr, M621_VOUT_DVS, (uint8_t)(code | M621_ENABLE_BIT), M621_VOLT_MASK);
}

static bool gpio6_set(const struct tg_gpu_power_io *io, bool enable)
{
    uint8_t ame, cfg;
    if (!io->read_pmic(io->context, TG_GPU_PMIC_MAX77620, M620_AME_GPIO, &ame))
        return false;
    /* Standard GPIO (clear pinmux bit), as max77620_config_gpio(). */
    if (!write_pmic_checked(io, TG_GPU_PMIC_MAX77620, M620_AME_GPIO,
                            (uint8_t)(ame & ~AME_GPIO6), AME_GPIO6))
        return false;
    cfg = enable ? GPIO6_ENABLE : (uint8_t)(0x02u /* input */ | 0x00u /* open drain */);
    return write_pmic_checked(io, TG_GPU_PMIC_MAX77620, M620_GPIO6, cfg, GPIO6_CFG_MASK);
}

static bool drain_mc(const struct tg_gpu_power_io *io)
{
    unsigned int attempt, sample;
    uint32_t request;
    update32(io, MC_FLUSH, MC_GPU_BIT, MC_GPU_BIT);
    request = io->read32(io->context, MC_FLUSH);
    if (request == UINT32_MAX || !(request & MC_GPU_BIT)) return false;
    for (attempt = 0; attempt < 100000; ++attempt) {
        uint32_t value;
        io->delay_us(io->context, 10);
        value = io->read32(io->context, MC_FLUSH_STATUS);
        if (value == UINT32_MAX) return false;
        if (!(value & MC_GPU_BIT))
            continue;
        for (sample = 0; sample < 5; ++sample)
            if (io->read32(io->context, MC_FLUSH_STATUS) != value)
                break;
        if (sample == 5)
            return true;
    }
    return false;
}

static bool restore_rail(const struct tg_gpu_power_io *io, const struct tg_gpu_power *s)
{
    bool ok;
    /* Drop EN first (rail off), then put the PMIC registers back so the
     * next owner (Horizon warmboot / Hekate POR defaults) sees them as found. */
    ok = io->write_pmic(io->context, TG_GPU_PMIC_MAX77620, M620_GPIO6, s->original_gpio6);
    ok = io->write_pmic(io->context, TG_GPU_PMIC_MAX77620, M620_AME_GPIO, s->original_ame) && ok;
    ok = io->write_pmic(io->context, s->pmic_address, M621_VOUT, s->original_enable) && ok;
    ok = io->write_pmic(io->context, s->pmic_address, M621_VOUT_DVS, s->original_voltage) && ok;
    ok = io->write_pmic(io->context, s->pmic_address, M621_CONTROL1, s->original_ctrl1) && ok;
    ok = io->write_pmic(io->context, s->pmic_address, M621_CONTROL2, s->original_ctrl2) && ok;
    return ok;
}

enum tg_gpu_power_result
tg_gpu_power_up(const struct tg_gpu_power_io *io, uint8_t pmic_address,
                struct tg_gpu_power *state)
{
    if (!io || !state || !io->read32 || !io->write32 || !io->read_pmic ||
        !io->write_pmic || !io->delay_us || pmic_address != TG_GPU_PMIC_MAX77621_GPU)
        return TG_GPU_POWER_BAD_ARGUMENT;
    if (state->owned)
        return TG_GPU_POWER_BUSY;
    state->stage = TG_GPU_STAGE_SNAPSHOT;
    if ((io->read32(io->context, APB_MISC_HIDREV) & HIDREV_T210_MASK) != HIDREV_T210_VALUE)
        return TG_GPU_POWER_UNSUPPORTED_BOARD;
    /* Do not reset or borrow an adapter already running somebody's channel. */
    if (!(io->read32(io->context, CAR_RESET_X) & GPU_GATE) ||
        !(io->read32(io->context, PMC_GPU_CLAMP) & 1u))
        return TG_GPU_POWER_BUSY;
    state->pmic_address = pmic_address;
    if (!io->read_pmic(io->context, TG_GPU_PMIC_MAX77620, M620_GPIO6, &state->original_gpio6) ||
        !io->read_pmic(io->context, TG_GPU_PMIC_MAX77620, M620_AME_GPIO, &state->original_ame) ||
        !io->read_pmic(io->context, pmic_address, M621_VOUT, &state->original_enable) ||
        !io->read_pmic(io->context, pmic_address, M621_VOUT_DVS, &state->original_voltage) ||
        !io->read_pmic(io->context, pmic_address, M621_CONTROL1, &state->original_ctrl1) ||
        !io->read_pmic(io->context, pmic_address, M621_CONTROL2, &state->original_ctrl2))
        return TG_GPU_POWER_PMIC_ERROR;
    state->original_clocks = io->read32(io->context, CAR_CLOCK_X);
    state->original_pllp_out5 = io->read32(io->context, CAR_PLLP_OUT5);
    state->original_mc_flush = io->read32(io->context, MC_FLUSH);
    state->owned = true;
    state->ready = false;
    state->stage = TG_GPU_STAGE_RAIL;

    /* Configure the buck, program the boot voltage while EN is still low,
     * then raise the EN pin. Hekate leaves GPIO6 low, so this is the first
     * time the GPU sees power since the bootloader. */
    if (!io->write_pmic(io->context, pmic_address, M621_CONTROL1, M621_CTRL1_HOS) ||
        !io->write_pmic(io->context, pmic_address, M621_CONTROL2, M621_CTRL2_HOS) ||
        !set_rail_voltage(io, pmic_address, tg_gpu_max77621_code(GPU_BOOT_UV), true) ||
        !gpio6_set(io, true))
        return TG_GPU_POWER_PMIC_ERROR;
    io->delay_us(io->context, 5000); /* ramp 12 mV/us plus settle */

    state->stage = TG_GPU_STAGE_CLOCKS;
    update32(io, CAR_PLLP_OUT5, OUT5_MASK, OUT5_204MHZ);
    io->delay_us(io->context, 2);
    if ((io->read32(io->context, CAR_PLLP_OUT5) & OUT5_MASK) != OUT5_204MHZ)
        return TG_GPU_POWER_CLOCK_ERROR;
    io->write32(io->context, CAR_CLOCK_SET, GPU_CLOCKS);
    io->delay_us(io->context, 10);
    if ((io->read32(io->context, CAR_CLOCK_X) & GPU_CLOCKS) != GPU_CLOCKS)
        return TG_GPU_POWER_CLOCK_ERROR;

    state->stage = TG_GPU_STAGE_RESET;
    io->write32(io->context, CAR_RESET_SET, GPU_GATE);
    io->delay_us(io->context, 10);
    update32(io, PMC_GPU_CLAMP, 1u, 0);
    io->delay_us(io->context, 10);
    io->write32(io->context, CAR_CLOCK_CLR, GPU_GATE);
    io->write32(io->context, CAR_RESET_CLR, GPU_GATE);
    io->write32(io->context, CAR_CLOCK_SET, GPU_GATE);
    if ((io->read32(io->context, CAR_RESET_X) & GPU_GATE) ||
        (io->read32(io->context, PMC_GPU_CLAMP) & 1u) ||
        (io->read32(io->context, CAR_CLOCK_X) & GPU_CLOCKS) != GPU_CLOCKS)
        return TG_GPU_POWER_RESET_ERROR;

    state->stage = TG_GPU_STAGE_MC;
    if (!drain_mc(io))
        return TG_GPU_POWER_MC_TIMEOUT;
    io->delay_us(io->context, 10);
    update32(io, MC_FLUSH, MC_GPU_BIT, 0);
    io->delay_us(io->context, 10);
    state->ready = true;
    state->stage = TG_GPU_STAGE_READY;
    return TG_GPU_POWER_OK;
}

enum tg_gpu_power_result
tg_gpu_power_drain(const struct tg_gpu_power_io *io, const struct tg_gpu_power *state)
{
    if (!state->owned || !state->ready) return TG_GPU_POWER_BAD_ARGUMENT;
    return drain_mc(io) ? TG_GPU_POWER_OK : TG_GPU_POWER_MC_TIMEOUT;
}

enum tg_gpu_power_result
tg_gpu_power_down(const struct tg_gpu_power_io *io, struct tg_gpu_power *state)
{
    bool ok;
    uint32_t clamp, reset;
    if (!state->owned)
        return TG_GPU_POWER_OK;
    if (state->stage >= TG_GPU_STAGE_MC && !drain_mc(io))
        return TG_GPU_POWER_MC_TIMEOUT;
    update32(io, PMC_GPU_CLAMP, 1u, 1u);
    io->delay_us(io->context, 10);
    clamp = io->read32(io->context, PMC_GPU_CLAMP);
    if (clamp == UINT32_MAX || !(clamp & 1u)) return TG_GPU_POWER_RESET_ERROR;
    io->write32(io->context, CAR_RESET_SET, GPU_GATE);
    io->delay_us(io->context, 10);
    reset = io->read32(io->context, CAR_RESET_X);
    if (reset == UINT32_MAX || !(reset & GPU_GATE)) return TG_GPU_POWER_RESET_ERROR;
    /* MC stays blocked and backing owned until clamp + reset are observed. */
    io->write32(io->context, CAR_CLOCK_CLR, GPU_CLOCKS & ~state->original_clocks);
    io->write32(io->context, CAR_CLOCK_SET, GPU_CLOCKS & state->original_clocks);
    update32(io, CAR_PLLP_OUT5, OUT5_MASK, state->original_pllp_out5);
    update32(io, MC_FLUSH, MC_GPU_BIT, state->original_mc_flush);
    ok = restore_rail(io, state);
    io->delay_us(io->context, 5000);
    state->ready = false;
    if (!ok)
        return TG_GPU_POWER_PMIC_ERROR;
    state->owned = false;
    return TG_GPU_POWER_OK;
}
