/* SPDX-License-Identifier: GPL-2.0-only
 * CPU -> EMC floor from this device's Noble Nintendo Switch Lite DT:
 * nx-plat.dtimg SHA256 86099dace65f2192e32845d448b5f4b038c58537f92fe80bd1c926049018e1a8,
 * /cpufreq/emc-scaling-data/emc-cpu-limit-table, all values in kHz.
 * Selection/maximum aggregation follows NVIDIA cpu_emc.c / emc_bwmgr.c,
 * kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b.
 * Copyright (c) 2014-2018 NVIDIA CORPORATION.
 * These are bandwidth requests, not extra trained EMC clock points.
 */
#ifndef SW_T214_EMC_CLIENT_FLOOR_H
#define SW_T214_EMC_CLIENT_FLOOR_H
#include "emc_governor.h"
static inline uint32_t sw_emc_cpu_floor_khz(uint32_t cpu_khz, uint32_t emc_max_khz)
{
    static const struct { uint32_t cpu,emc; } limits[] = {
        {102000,68000},{204800,102000},{307200,204000},{403000,408000},
        {710400,665600},{921600,1600000},{1581000,3200000}
    };
    uint32_t floor=0;
    for (unsigned i=0;i<sizeof(limits)/sizeof(limits[0]);++i) {
        if (cpu_khz<limits[i].cpu) break;
        floor=limits[i].emc;
    }
    return floor>emc_max_khz ? emc_max_khz : floor;
}
static inline uint32_t sw_emc_client_target(const struct sw_emc_control *c,
    uint32_t activity_khz, uint32_t cpu_khz, uint32_t display_floor_khz)
{
    if (!c || !c->ready || c->fault || !c->rate_count || !activity_khz ||
        !display_floor_khz) return 0;
    uint32_t maximum=c->rates[c->rate_count-1u];
    if (activity_khz>maximum) return 0;
    uint32_t cpu_floor=sw_emc_cpu_floor_khz(cpu_khz,maximum);
    uint32_t target=activity_khz>cpu_floor ? activity_khz : cpu_floor;
    return sw_emc_select(c,target,sw_emc_runtime_floor(display_floor_khz),0);
}
#endif
