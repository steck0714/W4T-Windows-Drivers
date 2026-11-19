/* SPDX-License-Identifier: GPL-2.0-only
 * Characterization data: Copyright (c) 2014-2019 NVIDIA CORPORATION;
 * Copyright (c) 2021-2023 CTCaer.
 * Ported from CTCaer/switch-l4t-kernel-4.9
 * 2d0059fd3167a8df756de2aa0489d4aa70a9fc15:
 *   drivers/soc/tegra/tegra210b01[-slt]-core-dvfs.c (EMC DVB)
 *   drivers/soc/tegra/tegra210-dvfs.c (gpub01_dvfs_table)
 * These are voltage requirements, NOT trained DRAM timing entries.
 */
#ifndef SWITCHFAN_EMC_POLICY_H
#define SWITCHFAN_EMC_POLICY_H
#include <stdint.h>
#include "platform/t214_emc_gpu_floor.h"

#define SW_EMC_CHARACTERIZED_MAX_KHZ 3200000u

/* T210B01 THRESHOLD_INDEX_2 from drivers/soc/tegra/fuse/speedo-tegra210.c
 * at the same revision. Caller validates the T210B01 fuse/handoff identity. */
static inline int sw_emc_soc_process(uint32_t speedo)
{
    if (!speedo || speedo == UINT32_MAX) return -1;
    return speedo < 1598u ? 0 : (speedo < 1709u ? 1 : 2);
}
static inline int sw_emc_gpu_process(uint32_t speedo)
{
    if (!speedo || speedo == UINT32_MAX) return -1;
    return speedo < 1584u ? 0 : (speedo < 1695u ? 1 : 2);
}

static inline uint32_t sw_emc_core_mv(uint32_t khz, uint32_t process,
                                     int aggressive_slt)
{
    static const struct {
        uint32_t khz;
        uint16_t normal[3], slt[3];
    } points[] = {
        { 204000, {637,637,637}, {600,600,600}},
        { 408000, {637,637,637}, {600,600,600}},
        { 800000, {637,637,637}, {600,600,600}},
        {1065600, {637,637,637}, {600,600,600}},
        {1331200, {650,637,637}, {650,600,600}},
        {1600000, {675,650,637}, {650,650,625}},
        {1866000, {700,675,637}, {700,675,637}},
        {2133000, {725,700,650}, {725,700,650}},
        {2400000, {750,725,675}, {750,725,675}},
        {2533000, {800,775,725}, {800,775,725}},
        {2666000, {825,800,750}, {825,800,750}},
        {2800000, {900,825,775}, {900,825,775}},
        {2933000, {1000,850,800}, {1000,850,800}},
        {3066000, {1025,900,850}, {1025,900,850}},
        {3200000, {1050,950,900}, {1050,950,900}},
    };
    uint32_t i;
    if (!khz || process >= 3) return 0;
    for (i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
        if (khz <= points[i].khz)
            return aggressive_slt ? points[i].slt[process] : points[i].normal[process];
    return 0;
}

static inline uint32_t sw_emc_gpu_mv(uint32_t khz, uint32_t process)
{
    /* GPU characterization has one owner in mesa-switch-woa. */
    uint32_t uv = tg_gpu_emc_floor_process_uv(process, khz);
    return uv == UINT32_MAX ? 0u : uv / 1000u;
}
#endif
