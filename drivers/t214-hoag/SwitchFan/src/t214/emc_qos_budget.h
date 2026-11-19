/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2014-2019 NVIDIA CORPORATION.
 * Copyright (c) 2021-2023 CTCaer.
 * LPDDR4 DDA divisors and CPU/RING1 terms derived from tegra21x_la.c at
 * 76e6d48970b451c242c20f298b8d63027836bb0b; clock arithmetic in emc_ptsa.h.
 * Pure planning only. It does not establish BPMP register ownership or make
 * a clock transition safe; the caller must implement and verify that contract.
 */
#ifndef SW_T214_EMC_QOS_BUDGET_H
#define SW_T214_EMC_QOS_BUDGET_H
#include "emc_ptsa.h"
#define SW_EMC_QOS_BUDGET_MAX_RATES 32u
struct sw_emc_qos_point {
    uint32_t khz;
    struct sw_emc_ptsa_clock clock;
    uint32_t ring;
};
struct sw_emc_qos_budget {
    uint32_t count, display;
    struct sw_emc_qos_point points[SW_EMC_QOS_BUDGET_MAX_RATES];
};
static inline int sw_emc_qos_budget_plan(const uint32_t *rates, uint32_t count,
    enum sw_emc_ptsa_policy policy, uint32_t panel_kbps, uint32_t minimum_display,
    uint32_t other_iso, uint32_t ring2, uint32_t extra_ring,
    struct sw_emc_qos_budget *out)
{
    struct sw_emc_qos_budget plan={0};
    uint32_t i;
    if (!out) return 0;
    *out=(struct sw_emc_qos_budget){0};
    if (!rates || !count || count>SW_EMC_QOS_BUDGET_MAX_RATES || !panel_kbps ||
        minimum_display>0xfffu || other_iso>4u*0xfffu ||
        ring2>0xfffu || extra_ring>0xfffu) return 0;
    plan.display=minimum_display;
    for (i=0;i<count;++i) {
        uint64_t numerator, denominator, display;
        struct sw_emc_qos_point *p=&plan.points[i];
        if (i && rates[i]<=rates[i-1u]) return 0;
        if (!sw_emc_ptsa_clock_values(rates[i],rates[count-1u],policy,&p->clock)) return 0;
        p->khz=rates[i];
        numerator=(uint64_t)p->clock.grant*panel_kbps*11u;
        denominator=(uint64_t)p->khz*16u*4u*10u;
        display=(numerator+denominator-1u)/denominator;
        if (!display || display>0xfffu) return 0;
        if (display>plan.display) plan.display=(uint32_t)display;
    }
    for (i=0;i<count;++i) {
        struct sw_emc_qos_point *p=&plan.points[i];
        uint64_t ring=((uint64_t)plan.display+other_iso+1u)/2u+
            p->clock.cpu_read+p->clock.cpu_write+ring2+extra_ring;
        /* Keep both ISO and RING1 below 100% of the corresponding clock's
         * decrement budget. A 13-bit register maximum is not a live budget. */
        if (ring>0xfffu || (uint64_t)plan.display*4u>=p->clock.grant ||
            ring*2u>=p->clock.grant) return 0;
        p->ring=(uint32_t)ring;
    }
    plan.count=count;
    *out=plan;
    return 1;
}
#endif
