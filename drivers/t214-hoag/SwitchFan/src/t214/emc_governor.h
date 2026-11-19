/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2016-2020 NVIDIA CORPORATION.
 * Copyright (c) 2021 CTCaer.
 * MC_ALL frequency-sampler demand and hysteresis adapted from
 * actmon_common.c / tegra210-soc-actmon.dtsi (revisions in actmon.h).
 * Hardware watermark events supply boost/decay; status polling never changes
 * the demand. No CPU-utilization proxy or extra software downclock delay.
 */
#ifndef SW_T214_EMC_GOVERNOR_H
#define SW_T214_EMC_GOVERNOR_H
#include "emc_control.h"
/* User-selected Windows floor (2026-11-15). This is independent of the
 * panel bandwidth reservation and does not invent a BPMP-trained rate. */
#define SW_EMC_RUNTIME_MIN_KHZ 204000u
static inline uint32_t sw_emc_runtime_floor(uint32_t display_floor_khz)
{
    if (!display_floor_khz) return 0;
    return display_floor_khz > SW_EMC_RUNTIME_MIN_KHZ ?
        display_floor_khz : SW_EMC_RUNTIME_MIN_KHZ;
}
/* Interrupt policy: preserve upstream integer percent order (10000/60=166),
 * with boost updated by the hardware watermark events, never by telemetry
 * reads. The Windows display floor and BPMP-trained OPP selection still apply. */
static inline uint32_t sw_emc_irq_target(const struct sw_emc_control *c,
    uint32_t average_active_khz, uint32_t boost_khz, uint32_t display_floor_khz)
{
    if (!c || !c->ready || c->fault || !c->rate_count || !display_floor_khz) return 0;
    uint32_t maximum=c->rates[c->rate_count-1u];
    if (average_active_khz>maximum || boost_khz>maximum) return 0;
    uint64_t demand=(uint64_t)average_active_khz*(10000u/60u)/100u+boost_khz;
    if (demand>maximum) demand=maximum;
    return sw_emc_select(c,(uint32_t)demand,sw_emc_runtime_floor(display_floor_khz),0);
}
/* The fixed 78 MHz, 32-bpp panel reserves peak pixel bandwidth, including
 * blanking, plus the upstream 10% catch-up margin. LPDDR4 has 16 bytes/Hz
 * and the T210 ISO efficiency table is 56% in this bandwidth range.
 * Caller also supplies the independently validated MC latency/QoS floor. */
static inline uint32_t sw_emc_panel_bw_floor(uint32_t pixel_khz, uint32_t bytes_per_pixel)
{
    uint64_t peak=(uint64_t)pixel_khz*bytes_per_pixel;
    if (!pixel_khz || !bytes_per_pixel || peak>560000u) return 0;
    return (uint32_t)((peak*110u + 16u*56u-1u)/(16u*56u));
}
#endif
