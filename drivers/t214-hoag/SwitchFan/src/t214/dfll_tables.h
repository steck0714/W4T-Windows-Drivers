/* SPDX-License-Identifier: GPL-2.0-only
 * T210B01 AggressiveSLT characterization and DFLL rate/LUT calculations.
 * Derived from NVIDIA/Switchroot clk-tegra124-dfll-fcpu.c, cvb.c and clk-dfll.c
 * at CTCaer/switch-l4t-kernel-4.9 2d0059fd3167a8df756de2aa0489d4aa70a9fc15.
 * Copyright (C) 2012-2018 NVIDIA Corporation. All rights reserved.
 * Copyright (c) 2021-2023, CTCaer
 * Original authors: Aleksandr Frid <afrid@nvidia.com>, Paul Walmsley <pwalmsley@nvidia.com>.
 * This table is for CLOSED-LOOP DFLL, never an open-loop PLLX voltage table.
 */
#ifndef SWITCHFAN_T214_DFLL_TABLES_H
#define SWITCHFAN_T214_DFLL_TABLES_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SW_DFLL_OPP_COUNT 23u
#define SW_DFLL_LUT_SIZE 33u
#define SW_DFLL_REF_KHZ 51000u
#define SW_DFLL_MIN_KHZ 102000u
#define SW_DFLL_MAX_UV 1235000u

struct sw_dfll_opp { uint32_t khz, uv; };
struct sw_dfll_table {
    struct sw_dfll_opp opp[SW_DFLL_OPP_COUNT];
    uint8_t lut[SW_DFLL_LUT_SIZE];
    uint32_t lut_size;
    uint32_t tune0_low, tune0_high, tune1;
    uint32_t out_rate_min_khz;
};
struct sw_dfll_request {
    uint32_t requested_khz, dvco_khz, scaled_khz;
    uint8_t mult, scale, lut_index;
};

static int64_t SwDfllRoundNearest(int64_t n, int64_t d)
{ return n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d; }

static uint32_t SwDfllCodeUv(uint8_t code)
{ return 250000u + (uint32_t)code * 5000u; }

static bool SwDfllBuildTable(uint32_t speedo, uint32_t process_id,
                             uint32_t emc_khz, struct sw_dfll_table *t)
{
    static const struct { uint32_t khz; int32_t c0, c1; } cvb[SW_DFLL_OPP_COUNT] = {
        {204000,732856,-17335}, {306000,760024,-18195}, {408000,789258,-19055},
        {510000,820558,-19915}, {612000,853926,-20775}, {714000,889361,-21625},
        {816000,926862,-22485}, {918000,966431,-23345}, {1020000,1008066,-24205},
        {1122000,1051768,-25065}, {1224000,1097537,-25925}, {1326000,1145373,-26785},
        {1428000,1195276,-27645}, {1581000,1274006,-28935}, {1683000,1329076,-29795},
        {1785000,1386213,-30655}, {1887000,1445416,-31515}, {1963500,1490873,-32155},
        {2065500,1553683,-33015}, {2091000,1580725,-33235}, {2193000,1580725,-33235},
        {2295000,1635431,-34095}, {2397000,1702903,-34955},
    };
    uint32_t i, j = 1, v, vmax, minimum_uv;
    if (!t || !speedo || speedo > 5000u || process_id > 1u) return false;
    memset(t, 0, sizeof(*t));
    minimum_uv = process_id == 0u && emc_khz > 2666000u ? 620000u : 600000u;
    t->tune0_low = process_id == 0u ? 0x0000ff90u : 0x0000ffa0u;
    t->tune0_high = 0x0000ffffu;
    t->tune1 = 0x021107ffu;
    for (i = 0; i < SW_DFLL_OPP_COUNT; ++i) {
        int64_t uv = SwDfllRoundNearest(113LL * speedo, 100);
        uv = SwDfllRoundNearest((uv + cvb[i].c1) * speedo, 100) + cvb[i].c0;
        uv = uv > 250000 ? ((uv - 250000 + 4999) / 5000) * 5000 + 250000 : 250000;
        if (uv < minimum_uv) uv = minimum_uv;
        if (uv > SW_DFLL_MAX_UV) uv = SW_DFLL_MAX_UV;
        t->opp[i] = (struct sw_dfll_opp){cvb[i].khz, (uint32_t)uv};
        if ((uint32_t)uv == minimum_uv) t->out_rate_min_khz = cvb[i].khz;
    }
    if (!t->out_rate_min_khz) return false;
    v = minimum_uv;
    vmax = t->opp[SW_DFLL_OPP_COUNT - 1u].uv;
    t->lut[0] = (uint8_t)((v - 250000u) / 5000u);
    for (i = 0; i < SW_DFLL_OPP_COUNT; ++i) {
        uint32_t vopp = t->opp[i].uv;
        for (;;) {
            uint32_t step, code;
            if (j >= SW_DFLL_LUT_SIZE) return false;
            step = (vmax - v) / (SW_DFLL_LUT_SIZE - j);
            v += step ? step : 1u;
            if (v >= vopp) break;
            /* Switchroot find_vdd_map_entry_min first truncates uV/step.
             * Preserve that intermediate-LUT rule; CVB OPPs above round up. */
            code = (v - 250000u) / 5000u;
            if (code != t->lut[j - 1u]) t->lut[j++] = (uint8_t)code;
        }
        v = j == SW_DFLL_LUT_SIZE - 1u ? vmax : vopp;
        if ((v - 250000u) / 5000u != t->lut[j - 1u]) {
            if (j >= SW_DFLL_LUT_SIZE) return false;
            t->lut[j++] = (uint8_t)((v - 250000u) / 5000u);
        }
        if (v >= vmax) break;
    }
    t->lut_size = j;
    return true;
}

static uint8_t SwDfllLutCeil(const struct sw_dfll_table *t, uint32_t uv)
{
    uint32_t i;
    for (i = 0; i < t->lut_size; ++i)
        if (SwDfllCodeUv(t->lut[i]) >= uv) return (uint8_t)i;
    return (uint8_t)(t->lut_size - 1u);
}

static uint8_t SwDfllLutFloor(const struct sw_dfll_table *t, uint32_t uv)
{
    uint32_t i;
    for (i = 0; i < t->lut_size; ++i)
        if (SwDfllCodeUv(t->lut[i]) > uv) return (uint8_t)(i ? i - 1u : 0u);
    return (uint8_t)(t->lut_size - 1u);
}

static bool SwDfllRateRequest(const struct sw_dfll_table *t, uint32_t khz,
                              uint32_t dvco_min_khz, struct sw_dfll_request *r)
{
    uint32_t rate = khz, scale = 256, mult, i;
    if (!t || !r || !t->lut_size || !dvco_min_khz || khz < SW_DFLL_MIN_KHZ ||
        khz > t->opp[SW_DFLL_OPP_COUNT - 1u].khz) return false;
    if (rate < dvco_min_khz) {
        scale = (uint32_t)(((uint64_t)rate * 256u + dvco_min_khz / 2u) / dvco_min_khz);
        if (!scale || scale > 256) return false;
        rate = dvco_min_khz;
    }
    mult = rate / (SW_DFLL_REF_KHZ / 2u);
    if (!mult || mult > 127) return false;
    r->requested_khz = khz;
    r->dvco_khz = mult * (SW_DFLL_REF_KHZ / 2u);
    r->scaled_khz = r->dvco_khz * scale / 256u;
    r->mult = (uint8_t)mult;
    r->scale = (uint8_t)(scale - 1u);
    for (i = 0; i < SW_DFLL_OPP_COUNT - 1u && t->opp[i].khz < r->dvco_khz; ++i) {}
    r->lut_index = SwDfllLutCeil(t, t->opp[i].uv);
    return true;
}
#endif
