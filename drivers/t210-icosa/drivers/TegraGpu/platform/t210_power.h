/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEGRA_GPU_T210_POWER_H
#define TEGRA_GPU_T210_POWER_H

#include <stdbool.h>
#include <stdint.h>

/* Erista (Tegra X1 / T210, GM20B "B1") GPU power ownership.
 *
 * The GPU rail is a MAX77621 (I2C5 0x1C). Its EN pin is driven by MAX77620
 * (I2C5 0x3C) GPIO6; Hekate drives that pin low before booting an OS, so the
 * GPU is unpowered when Windows starts. This module therefore sequences:
 *   rail (GPIO6 + MAX77621) -> 204 MHz PLLP_OUT5 + GPU clocks -> reset
 *   -> unclamp -> reset release -> MC drain/unflush.
 * The caller serializes shared CAR/PMC/MC and I2C5 accesses. It does not
 * touch CPU/DDR rails, PLLP itself, GPCPLL, mappings or GPU firmware.
 */
struct tg_gpu_power_io {
    void *context;
    uint32_t (*read32)(void *context, uint32_t address);
    void (*write32)(void *context, uint32_t address, uint32_t value);
    /* address is the 7-bit I2C address: 0x1C (MAX77621 GPU) or 0x3C (MAX77620). */
    bool (*read_pmic)(void *context, uint8_t address, uint8_t reg, uint8_t *value);
    bool (*write_pmic)(void *context, uint8_t address, uint8_t reg, uint8_t value);
    void (*delay_us)(void *context, uint32_t delay);
};

enum tg_gpu_power_result {
    TG_GPU_POWER_OK,
    TG_GPU_POWER_UNSUPPORTED_BOARD,
    TG_GPU_POWER_BUSY,
    TG_GPU_POWER_PMIC_ERROR,
    TG_GPU_POWER_CLOCK_ERROR,
    TG_GPU_POWER_RESET_ERROR,
    TG_GPU_POWER_MC_TIMEOUT,
    TG_GPU_POWER_BAD_ARGUMENT
};

enum tg_gpu_power_stage {
    TG_GPU_STAGE_SNAPSHOT,
    TG_GPU_STAGE_RAIL,
    TG_GPU_STAGE_CLOCKS,
    TG_GPU_STAGE_RESET,
    TG_GPU_STAGE_MC,
    TG_GPU_STAGE_READY
};

#define TG_GPU_PMIC_MAX77621_GPU 0x1Cu
#define TG_GPU_PMIC_MAX77620     0x3Cu

struct tg_gpu_power {
    bool owned;
    bool ready;
    uint8_t pmic_address;      /* MAX77621 GPU, always 0x1C */
    uint8_t original_enable;   /* MAX77621 VOUT (0x00) before acquisition */
    uint8_t original_voltage;  /* MAX77621 VOUT_DVS (0x01) before acquisition */
    uint8_t original_ctrl1;    /* MAX77621 CONTROL1 */
    uint8_t original_ctrl2;    /* MAX77621 CONTROL2 */
    uint8_t original_gpio6;    /* MAX77620 GPIO6 config */
    uint8_t original_ame;      /* MAX77620 AME_GPIO pinmux */
    uint32_t original_clocks;
    uint32_t original_pllp_out5;
    uint32_t original_mc_flush;
    enum tg_gpu_power_stage stage;
};

/* Zero-initialize state before the first call. Even a failed power_up may
 * own partially changed resources; call power_down before releasing them.
 * A failed MC drain keeps ownership, so live GPU memory cannot be reclaimed.
 * pmic_address must be TG_GPU_PMIC_MAX77621_GPU.
 */
enum tg_gpu_power_result tg_gpu_power_up(const struct tg_gpu_power_io *io,
                                        uint8_t pmic_address,
                                        struct tg_gpu_power *state);
enum tg_gpu_power_result tg_gpu_power_down(const struct tg_gpu_power_io *io,
                                          struct tg_gpu_power *state);
/* Channel fetch must already be stopped. Successful drain holds the GPU
 * MC client in reset while the caller stops its PLL before power_down.
 */
enum tg_gpu_power_result tg_gpu_power_drain(const struct tg_gpu_power_io *io,
                                           const struct tg_gpu_power *state);

/* MAX77621 voltage helpers: V = 606250 uV + 6250 uV * code, code 0..127. */
#define TG_GPU_MAX77621_BASE_UV 606250u
#define TG_GPU_MAX77621_STEP_UV 6250u
static inline uint8_t tg_gpu_max77621_code(uint32_t microvolts)
{
    uint32_t code;
    if (microvolts < TG_GPU_MAX77621_BASE_UV) return 0;
    code = (microvolts - TG_GPU_MAX77621_BASE_UV + TG_GPU_MAX77621_STEP_UV - 1u) /
           TG_GPU_MAX77621_STEP_UV;
    return (uint8_t)(code > 0x7fu ? 0x7fu : code);
}

#endif
