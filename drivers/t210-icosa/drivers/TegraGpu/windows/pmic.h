/* SPDX-License-Identifier: MIT */
#ifndef TEGRA_GPU_PMIC_H
#define TEGRA_GPU_PMIC_H
#include <stdint.h>
/* Erista: the GPU rail (MAX77621 0x1C) and its EN pin (MAX77620 0x3C GPIO6)
 * are reached directly through ACPI I2C5 connection resources over the SPB
 * Resource Hub. There is no SwitchFan bridge on Erista. The request struct is
 * kept so the shared worker/diagnostic code keeps its ABI. QUERY_* codes are
 * answered locally by platform_io.c.
 */
#define TG_GPU_IOCTL_PMIC 0x00222410u
#define TG_GPU_PMIC_QUERY_ADDRESS 0x100u
#define TG_GPU_PMIC_QUERY_PLLP    0x101u
#define TG_GPU_PMIC_QUERY_TEMP    0x102u
struct tg_gpu_pmic_request {
    uint32_t address;
    uint32_t reg;
    uint32_t write;
    uint32_t value;
};
#endif
