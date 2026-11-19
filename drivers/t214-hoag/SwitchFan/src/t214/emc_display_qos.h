/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2014-2019 NVIDIA CORPORATION.
 * Copyright (c) 2021-2023 CTCaer.
 * T210 LPDDR4 display bandwidth/latency rules adapted from tegra21x_la.c,
 * la_priv.h and mc.c at switch-l4t-kernel-nvidia
 * 76e6d48970b451c242c20f298b8d63027836bb0b.
 *
 * Scope: the Lite's one unscaled 78 MHz, 32-bpp DC window A. Rotation changes
 * the backing contents, not the physical panel's scan geometry. No camera,
 * second display or MPO producer is introduced by this implementation.
 * BPMP owns EMC timing transitions. Its post-transition grant/CPU behavior
 * still needs validation on the Windows firmware; successful reset bootstrap
 * does not prove that contract. Windows bootstraps an uninitialized PTSA.
 * Windows owns DIS/RING1 and the explicitly calculated clock terms. Prepare
 * lower terms before a downshift; provide parent margin before an upshift,
 * then finish its terms after the clock is confirmed. The complete zero clock
 * tuple observed on B01 has its own completion callback. Partial/unknown
 * tuples remain errors, not permission to rewrite an unrecognized MC state.
 * Existing configurations are retained. This layer never writes an EMC PLL,
 * timing, translation or latency-allowance register; bootstrap is separate.
 */
#ifndef SW_T214_EMC_DISPLAY_QOS_H
#define SW_T214_EMC_DISPLAY_QOS_H
#include <stdint.h>
#include "emc_governor.h"
#include "emc_qos_budget.h"
enum sw_emc_qos_register {
    SwQosDisRate, SwQosDisMin, SwQosDisMax,
    SwQosRingRate, SwQosRingMin, SwQosRingMax,
    SwQosGrant, SwQosCpuRead, SwQosCpuWrite,
    SwQosDisbRate, SwQosVeRate, SwQosVe2Rate, SwQosIspRate, SwQosRing2Rate,
    SwQosDcLatency, SwQosDcScaledLatency, SwQosDramConfig, SwQosRegisterCount
};
struct sw_emc_qos_io {
    void *context;
    uint32_t (*read)(void *, enum sw_emc_qos_register);
    void (*write)(void *, enum sw_emc_qos_register, uint32_t);
    int (*bootstrap)(void *, uint32_t current_khz, uint32_t display_rate,
                     uint32_t ring_rate);
    int (*scale)(void *, const struct sw_emc_ptsa_clock *, uint32_t ring_rate);
    int (*finish_zero_clock)(void *, const struct sw_emc_ptsa_clock *, uint32_t ring_rate);
};
struct sw_emc_qos {
    uint32_t minimum_khz, display_rate, ring_rate, latency, scaled_latency;
    int prepared;
    struct sw_emc_qos_budget budget;
    uint32_t active_terms, pending_terms, iso_rates[4], ring2_rate;
    int pending;
    /* Temporary, bounded snapshot of the initial read, never extra MMIO. */
    uint32_t prepare_readback[SwQosRegisterCount];
    int prepare_read_error;
    /* Keep the post-clock sample distinct from the initial reset snapshot. */
    uint32_t verify_readback[SwQosRegisterCount];
    int verify_read_error;
};
#define SW_QOS_PANEL_KBPS (78000u * 4u)
#define SW_QOS_GRANT_MAX 0x1fffu
/* Upstream LPDDR4 CPU read=9%/2, write=1% of the grant; rounded upward. */
#define SW_QOS_CPU_READ_MAX ((SW_QOS_GRANT_MAX * 9u + 199u) / 200u)
#define SW_QOS_CPU_WRITE_MAX ((SW_QOS_GRANT_MAX + 99u) / 100u)

static inline uint32_t sw_qos_div_ceil(uint64_t n, uint64_t d)
{ return (uint32_t)((n + d - 1u) / d); }

static inline int sw_emc_qos_latency_fits(uint32_t khz, uint32_t la)
{
    uint64_t refill_ns, arbitration_ns;
    if (!khz || la > 255u) return 0;
    /* mc.c supplies a 2000 ns DVFS latency in this pinned T210 path. Deduct
     * those bytes from the 6144-byte MCCIF alone, without relying on the much
     * larger 151552-byte window-A line buffer. Preserve the existing LA so its
     * lower drain-time bound is unchanged. LA units are 30 ns. */
    refill_ns = (6144u - sw_qos_div_ceil((uint64_t)SW_QOS_PANEL_KBPS*2000u,1000000u));
    refill_ns = refill_ns*10000000u/(SW_QOS_PANEL_KBPS*11u);
    arbitration_ns = sw_qos_div_ceil(158000000u,khz); /* 70 + 88 EMC clocks */
    return refill_ns > arbitration_ns && la*30u <= refill_ns-arbitration_ns;
}

static inline int sw_emc_qos_fields(const uint32_t *v)
{
    uint32_t i;
    if ((v[SwQosDramConfig]&3u)!=1u ||
        v[SwQosGrant]>SW_QOS_GRANT_MAX ||
        v[SwQosCpuRead]>SW_QOS_CPU_READ_MAX ||
        v[SwQosCpuWrite]>SW_QOS_CPU_WRITE_MAX) return SwEmcProtocol;
    for (i=SwQosDisbRate;i<=SwQosRing2Rate;++i)
        if (v[i]>0xfffu) return SwEmcProtocol;
    if (v[SwQosDisRate]>0xfffu || v[SwQosRingRate]>0xfffu) return SwEmcProtocol;
    return 0;
}

static inline int sw_emc_qos_read(const struct sw_emc_qos_io *io, uint32_t *v)
{
    uint32_t i;
    int err;
    for (i=0;i<SwQosRegisterCount;++i) {
        v[i]=io->read(io->context,(enum sw_emc_qos_register)i);
        if (v[i]==UINT32_MAX) return SwEmcIo;
    }
    err=sw_emc_qos_fields(v);
    if (err) return err;
    return v[SwQosGrant] ? 0 : SwEmcProtocol;
}

static inline uint32_t sw_emc_qos_find(const struct sw_emc_qos *q, uint32_t khz)
{
    for (uint32_t i=0;i<q->budget.count;++i)
        if (q->budget.points[i].khz==khz) return i;
    return UINT32_MAX;
}
static inline int sw_emc_qos_clock_matches(const uint32_t *v,
                                         const struct sw_emc_ptsa_clock *clock)
{
    return v[SwQosGrant]==clock->grant && v[SwQosCpuRead]==clock->cpu_read &&
        v[SwQosCpuWrite]==clock->cpu_write;
}
static inline int sw_emc_qos_capture(struct sw_emc_qos *q,
    const struct sw_emc_qos_io *io, uint32_t *v)
{
    for (uint32_t i=0;i<SwQosRegisterCount;++i) v[i]=0;
    int err=sw_emc_qos_read(io,v);
    q->verify_read_error=err;
    for (uint32_t i=0;i<SwQosRegisterCount;++i) q->verify_readback[i]=v[i];
    return err;
}
static inline int sw_emc_qos_nonclock(const struct sw_emc_qos *q, const uint32_t *v)
{
    if (v[SwQosDisRate]!=q->display_rate || v[SwQosRingRate]!=q->ring_rate ||
        (v[SwQosDisMin]&0x3fu)!=0x3bu || (v[SwQosDisMax]&0x3fu)!=31u ||
        (v[SwQosRingMin]&0x3fu)!=0x3bu || (v[SwQosRingMax]&0x3fu)!=31u ||
        v[SwQosDcLatency]!=q->latency || v[SwQosDcScaledLatency]!=q->scaled_latency ||
        v[SwQosRing2Rate]!=q->ring2_rate) return SwEmcMismatch;
    for (uint32_t i=0;i<4u;++i)
        if (v[SwQosDisbRate+i]!=q->iso_rates[i]) return SwEmcMismatch;
    return 0;
}
static inline int sw_emc_qos_budget_fits(const struct sw_emc_qos *q,
    const uint32_t *v, uint32_t physical_khz)
{
    /* Nonzero CPU reservations are required; only the separate, observed
     * all-zero firmware-reset path may initialize missing clock terms. */
    if (!physical_khz || !v[SwQosGrant] || !v[SwQosCpuRead] || !v[SwQosCpuWrite])
        return SwEmcProtocol;
    uint64_t iso=q->display_rate;
    for (uint32_t i=0;i<4u;++i) iso+=q->iso_rates[i];
    if ((uint64_t)q->display_rate*physical_khz*640u <
        (uint64_t)v[SwQosGrant]*SW_QOS_PANEL_KBPS*11u ||
        q->display_rate*4u>=v[SwQosGrant] || q->ring_rate*2u>=v[SwQosGrant] ||
        q->ring_rate < sw_qos_div_ceil(iso,2u)+v[SwQosCpuRead]+v[SwQosCpuWrite]+q->ring2_rate)
        return SwEmcMismatch;
    return 0;
}
static inline int sw_emc_qos_values(const struct sw_emc_qos *q,
    const uint32_t *v, uint32_t physical_khz, uint32_t terms)
{
    int err=sw_emc_qos_nonclock(q,v);
    if (err) return err;
    if (terms>=q->budget.count ||
        !sw_emc_qos_clock_matches(v,&q->budget.points[terms].clock)) return SwEmcProtocol;
    return sw_emc_qos_budget_fits(q,v,physical_khz);
}
static inline int sw_emc_qos_check(struct sw_emc_qos *q,
    const struct sw_emc_qos_io *io, uint32_t khz)
{
    uint32_t v[SwQosRegisterCount];
    uint32_t index=sw_emc_qos_find(q,khz);
    if (!q->prepared || q->pending || index==UINT32_MAX) return SwEmcNotReady;
    int err=sw_emc_qos_capture(q,io,v);
    if (err) return err;
    if (q->active_terms!=index || q->ring_rate!=q->budget.points[index].ring)
        return SwEmcMismatch;
    return sw_emc_qos_values(q,v,khz,index);
}
static inline int sw_emc_qos_prepare(struct sw_emc_qos *q,
    const struct sw_emc_qos_io *io, const uint32_t *rates, uint32_t count,
    uint32_t before_khz, uint32_t after_khz)
{
    uint32_t v[SwQosRegisterCount]={0}, before_index=UINT32_MAX, after_index=UINT32_MAX;
    int err, bootstrap=0;
    if (!q || !io || !io->read || !io->write || !rates || !count ||
        count>SW_EMC_QOS_BUDGET_MAX_RATES) return SwEmcInvalid;
    for (uint32_t i=0;i<count;++i) {
        if (i && rates[i]<=rates[i-1u]) return SwEmcInvalid;
        if (rates[i]==before_khz) before_index=i;
        if (rates[i]==after_khz) after_index=i;
    }
    if (before_index==UINT32_MAX || after_index==UINT32_MAX) return SwEmcInvalid;
    if (q->pending) return SwEmcNotReady;
    if (before_index!=after_index && !io->scale) return SwEmcNotReady;
    if (!q->prepared) {
        uint32_t iso=0, extra=0, minimum_display=0, ring2;
        struct sw_emc_ptsa_clock current;
        err=sw_emc_qos_read(io,v);
        q->prepare_read_error=err;
        for (uint32_t i=0;i<SwQosRegisterCount;++i) q->prepare_readback[i]=v[i];
        bootstrap=(err==SwEmcProtocol && !v[SwQosGrant] && !sw_emc_qos_fields(v)) ||
            (!err && sw_emc_ptsa_mtc_seed(before_khz,rates[count-1u],
                v[SwQosGrant],v[SwQosCpuRead],v[SwQosCpuWrite]) &&
             !v[SwQosDisRate] && !v[SwQosRingRate] &&
             v[SwQosDisMin]==63u && v[SwQosDisMax]==63u &&
             v[SwQosRingMin]==63u && v[SwQosRingMax]==63u);
        if (bootstrap && !io->bootstrap) return SwEmcNotReady;
        if (err && (!bootstrap || !io->bootstrap)) return err;
        if (!sw_emc_qos_latency_fits(rates[0],v[SwQosDcLatency]&0xffu) ||
            !sw_emc_qos_latency_fits(rates[0],v[SwQosDcScaledLatency]&0xffu) ||
            !sw_emc_qos_latency_fits(rates[0],(v[SwQosDcScaledLatency]>>16)&0xffu) ||
            !sw_emc_ptsa_clock_values(before_khz,rates[count-1u],SW_EMC_PTSA_POLICY_STANDARD,&current))
            return SwEmcInvalid;
        for (uint32_t i=0;i<4u;++i) iso+=v[SwQosDisbRate+i];
        ring2=bootstrap ? 12u : v[SwQosRing2Rate];
        if (!bootstrap) {
            if (!sw_emc_qos_clock_matches(v,&current)) return SwEmcProtocol;
            minimum_display=v[SwQosDisRate];
            /* Preserve surplus over the observed children. A lower old DIS
             * does not prove this reservation can fund new display demand. */
            uint32_t base=sw_qos_div_ceil((uint64_t)minimum_display+iso,2u)+
                current.cpu_read+current.cpu_write+ring2;
            if (v[SwQosRingRate]>base) extra=v[SwQosRingRate]-base;
        }
        if (!sw_emc_qos_budget_plan(rates,count,SW_EMC_PTSA_POLICY_STANDARD,
            SW_QOS_PANEL_KBPS,minimum_display,iso,ring2,extra,&q->budget)) return SwEmcInvalid;
        /* Every adjacent upshift must fit the old grant so all points remain
         * reachable after a downshift. The governor takes trained intermediate
         * steps; the direct-transaction check below still rejects unsafe jumps.
         * Validate this before bootstrap/MMIO/clock mutation. */
        for (uint32_t from=0;from+1u<count;++from)
            if (q->budget.points[from+1u].ring*2u>=q->budget.points[from].clock.grant)
                return SwEmcInvalid;
        q->minimum_khz=rates[0]; q->display_rate=q->budget.display;
        q->ring_rate=q->budget.points[before_index].ring;
        q->latency=v[SwQosDcLatency]; q->scaled_latency=v[SwQosDcScaledLatency];
        for (uint32_t i=0;i<4u;++i) q->iso_rates[i]=v[SwQosDisbRate+i];
        q->ring2_rate=ring2; q->active_terms=before_index;
        if (bootstrap) {
            err=io->bootstrap(io->context,before_khz,q->display_rate,q->ring_rate);
            if (err) return err;
        } else {
            io->write(io->context,SwQosRingMin,(v[SwQosRingMin]&~0x3fu)|0x3bu);
            io->write(io->context,SwQosRingMax,(v[SwQosRingMax]&~0x3fu)|31u);
            io->write(io->context,SwQosRingRate,q->ring_rate);
            io->write(io->context,SwQosDisMin,(v[SwQosDisMin]&~0x3fu)|0x3bu);
            io->write(io->context,SwQosDisMax,(v[SwQosDisMax]&~0x3fu)|31u);
            io->write(io->context,SwQosDisRate,q->display_rate);
        }
        q->prepared=1;
        err=sw_emc_qos_check(q,io,before_khz);
        if (err) { q->prepared=0; return err; }
    } else {
        if (q->budget.count!=count) return SwEmcInvalid;
        for (uint32_t i=0;i<count;++i)
            if (q->budget.points[i].khz!=rates[i]) return SwEmcInvalid;
        err=sw_emc_qos_check(q,io,before_khz);
        if (err) return err;
    }
    if (before_index==after_index) return 0;
    const struct sw_emc_qos_point *target=&q->budget.points[after_index];
    if (after_khz<before_khz) {
        err=io->scale(io->context,&target->clock,target->ring);
        if (err) return err;
        q->active_terms=after_index;
        q->ring_rate=target->ring;
    } else {
        /* Parent credits must cover CPUs even if firmware updates CPU terms
         * before Windows receives the clock reply. Refuse an unrepresentable
         * direct jump instead of overcommitting the old grant. Current three
         * trained clocks support every pair; a larger future table may need
         * intermediate trained steps, not an invented frequency. */
        if (target->ring*2u>=q->budget.points[before_index].clock.grant)
            return SwEmcInvalid;
        io->write(io->context,SwQosRingRate,target->ring);
        q->ring_rate=target->ring;
    }
    err=sw_emc_qos_capture(q,io,v);
    if (!err) err=sw_emc_qos_values(q,v,before_khz,q->active_terms);
    if (err) return err;
    q->pending_terms=after_index; q->pending=1;
    return 0;
}
static inline int sw_emc_qos_verify(struct sw_emc_qos *q,
    const struct sw_emc_qos_io *io, uint32_t actual_khz)
{
    uint32_t v[SwQosRegisterCount];
    if (!q->pending) return sw_emc_qos_check(q,io,actual_khz);
    if (!q->prepared || q->pending_terms>=q->budget.count ||
        actual_khz!=q->budget.points[q->pending_terms].khz) return SwEmcMismatch;
    const struct sw_emc_qos_point *target=&q->budget.points[q->pending_terms];
    int err=sw_emc_qos_capture(q,io,v);
    /* Native B01 firmware overwrites the three clock terms with zero. Only
     * recognize that exact tuple in a pending, actual-clock-verified change;
     * an idle check or partial zero still fails without writing anything. */
    const int reset_clock=err==SwEmcProtocol && !sw_emc_qos_fields(v) &&
        !v[SwQosGrant] && !v[SwQosCpuRead] && !v[SwQosCpuWrite];
    if (err && !reset_clock) return err;
    err=sw_emc_qos_nonclock(q,v); if (err) return err;
    if (reset_clock) {
        if (!io->finish_zero_clock) return SwEmcProtocol;
        err=io->finish_zero_clock(io->context,&target->clock,target->ring);
        if (err) return err;
    } else if (!sw_emc_qos_clock_matches(v,&target->clock)) {
        /* MTC owns the clock boundary and rewrites these three terms (native
         * 204MHz:502/23/3;1331.2MHz:3272/148/17). Their policy/rounding need
         * not equal Windows' terms. This is not an arbitrary-value whitelist:
         * require a pending GET_RATE-confirmed target, unchanged non-clock
         * ownership above, and a positive, physically sufficient display and
         * parent/child budget below. Normalize only our owned clock fields.
         * The scaler proves every intermediate budget/latch, rolls back on
         * failure, and the final exact Windows tuple is checked before success.
         * Idle checks and reset bootstrap retain their separate strict rules. */
        err=sw_emc_qos_budget_fits(q,v,actual_khz);
        if (err) return err;
        if (!io->scale) return SwEmcNotReady;
        err=io->scale(io->context,&target->clock,target->ring);
        if (err) return err;
    }
    err=sw_emc_qos_capture(q,io,v);
    if (!err) err=sw_emc_qos_values(q,v,actual_khz,q->pending_terms);
    if (err) return err;
    q->active_terms=q->pending_terms; q->pending=0;
    return 0;
}
#endif
