/* SPDX-License-Identifier: MIT */
#ifndef TEGRA_GPU_T210_CLOCK_H
#define TEGRA_GPU_T210_CLOCK_H

#include "t210_power.h"

/* Fixed bring-up profiles for Erista (GM20B B1, legacy GPCPLL), not a governor.
 * GPCCLK = 38.4 MHz * N / PL_div / 2, VCO = 38.4 MHz * N (1.3 .. 2.6 GHz).
 *   460.8 MHz: N=48, PL=2 (VCO 1843.2 MHz) (rail from speedo, >= 950 mV)   <- default
 *   768.0 MHz: N=40, PL=1 (VCO 1536.0 MHz) (rail from speedo; low bins are refused)   <- opt-in
 */
#define TG_GPU_FIXED_KHZ 460800u
#define TG_GPU_OVERCLOCK_KHZ 768000u
#define TG_GPU_REFERENCE_KHZ 38400u
#define TG_GPU_CLOCK_SAVED_REGS 6u

enum tg_gpu_clock_result {
    TG_GPU_CLOCK_OK, TG_GPU_CLOCK_BAD_STATE, TG_GPU_CLOCK_RAIL_ERROR,
    TG_GPU_CLOCK_CALIBRATION_ERROR, TG_GPU_CLOCK_READBACK_ERROR,
    TG_GPU_CLOCK_COUNTER_ERROR
};

struct tg_gpu_clock {
    bool owned;
    bool running;
    uint32_t saved[TG_GPU_CLOCK_SAVED_REGS];
    uint32_t measured_khz;
    uint32_t target_khz;
    uint32_t rail_uv;
    uint32_t counter;
    uint32_t stage;
    /* Kept for struct compatibility with the shared diagnostics; the legacy
     * (non-NA) PLL has no ADC calibration, so these stay zero. */
    int32_t adc_slope_uv;
    int32_t adc_intercept_uv;
    int32_t external_calibration;
};

/* Requires an idle GPU just acquired by power_up, no live DMA and exclusive
 * register ownership. Zero state first. Uses the legacy (B1) GPCPLL path as
 * in Switchroot clk_gm20b.c: bypass, program M/N/PL, enable, wait for LOCK,
 * SYNC_MODE, then switch SEL_VCO. NA/DVFS registers are never written.
 * Only the GPU MAX77621 voltage is set, before PLL startup; power_down
 * restores it after the clock is stopped. CPU/DDR are untouched.
 * Stop channels and drain their writes before calling clock_stop.
 */
enum tg_gpu_clock_result tg_gpu_clock_start(const struct tg_gpu_power_io *io,
    const struct tg_gpu_power *power, uint32_t target_khz, struct tg_gpu_clock *clock);
enum tg_gpu_clock_result tg_gpu_clock_measure(const struct tg_gpu_power_io *io,
    struct tg_gpu_clock *clock);
enum tg_gpu_clock_result tg_gpu_clock_stop(const struct tg_gpu_power_io *io,
    struct tg_gpu_clock *clock);

/* Rail voltage (uV) for a profile, from the chip's own GPU speed bin.
 * raw_speedo is the FUSE_CPU_SPEEDO_2 shadow (0x7000F930). Uses the Erista
 * CVB table of Linux nouveau gm20b (NVIDIA, MIT) with a 950 mV floor, rounded
 * up to the MAX77621 6.25 mV grid. UINT32_MAX when the fuse is 0/unreadable,
 * the profile is unsupported, or the need exceeds the 1.150 V rail limit
 * (the driver then refuses to start rather than under-volt).
 */
uint32_t tg_gpu_clock_required_uv(uint32_t raw_speedo, uint32_t target_khz);

#endif
