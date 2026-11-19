/* SPDX-License-Identifier: GPL-2.0-only
 * Runtime EMC control core. Caller serializes the entire transaction.
 * BPMP clock protocol: NVIDIA clk-nv-bpmp.c and bpmp_t210_abi.h,
 * CTCaer/switch-l4t-kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b.
 * Never write an EMC PLL or timing register from this module: Hekate/BPMP
 * must already have a DRAM-specific trained timing table.
 */
#ifndef SWITCHFAN_EMC_CONTROL_H
#define SWITCHFAN_EMC_CONTROL_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "emc_policy.h"

#define SW_EMC_MAX_RATES 32u
#define SW_EMC_MRQ_CLK 22u
enum { SwEmcOk = 0, SwEmcInvalid = -1, SwEmcProtocol = -2,
       SwEmcIo = -3, SwEmcMismatch = -4, SwEmcFault = -5,
       SwEmcNotReady = -6 };

/* Wire layout is command@0, reserved@4, signed 64-bit Hz@8. */
struct sw_emc_rate_request { uint32_t command, reserved; uint64_t hz; };

struct sw_emc_ops {
    int (*message)(void *, uint32_t mrq, const void *, uint32_t, void *, uint32_t);
    /* Atomically publish an EMC-owned VDD_CORE/GPU floor to their owners,
     * raise both rails and verify voltage before returning success.
     * GPU DVFS must preserve this vote, including power-gate/unload paths.
     * Partial failure must retain any raised vote. Do not use a bare PMIC
     * write here: another GPU transition could immediately lower it again. */
    int (*raise_floors)(void *, uint32_t core_mv, uint32_t gpu_mv);
    /* Called only after a confirmed frequency change. Remove only this
     * client's excess vote; preserve CPU/display/other clients' rail floors.
     * Failure must retain the higher votes. */
    int (*settle_floors)(void *, uint32_t core_mv, uint32_t gpu_mv);
    /* Display/ISO arbitration must cover both clocks before SET_RATE.
     * Check the firmware-owned readbacks again before lowering rail votes. */
    int (*prepare_qos)(void *, uint32_t before_khz, uint32_t after_khz);
    int (*verify_qos)(void *, uint32_t actual_khz);
};

struct sw_emc_control {
    struct sw_emc_ops ops;
    void *context;
    uint32_t rates[SW_EMC_MAX_RATES], rate_count;
    uint32_t clock_id, soc_process, gpu_process;
    uint32_t boot_khz, actual_khz, requested_khz, core_floor_mv, gpu_floor_mv;
    /* Initial discovery diagnostics; no public status ABI change. */
    uint32_t probe_stage, round_request_khz;
    uint64_t round_reply_hz;
    int round_status;
    int aggressive_slt, ready, fault, last_error;
};

static inline int sw_emc_read_rate(struct sw_emc_control *c, uint32_t *khz)
{
    uint32_t command = (1u << 24) | c->clock_id;
    uint64_t hz = 0;
    int err = c->ops.message(c->context, SW_EMC_MRQ_CLK, &command,
                             sizeof(command), &hz, sizeof(hz));
    *khz = 0;
    if (err) return err;
    if (!hz || hz > (uint64_t)SW_EMC_CHARACTERIZED_MAX_KHZ * 1000u || hz % 1000u)
        return SwEmcProtocol;
    *khz = (uint32_t)(hz / 1000u);
    return SwEmcOk;
}

static inline int sw_emc_round_rate(struct sw_emc_control *c, uint32_t khz,
                                   uint32_t *rounded)
{
    struct sw_emc_rate_request req = {(3u << 24) | c->clock_id, 0,
                                    (uint64_t)khz * 1000u};
    uint64_t hz = 0;
    int err = c->ops.message(c->context, SW_EMC_MRQ_CLK, &req, sizeof(req),
                             &hz, sizeof(hz));
    c->round_request_khz = khz;
    c->round_reply_hz = hz;
    c->round_status = err;
    if (err) return err;
    if (!hz || hz % 1000u || hz > (uint64_t)SW_EMC_CHARACTERIZED_MAX_KHZ * 1000u)
        return SwEmcProtocol;
    *rounded = (uint32_t)(hz / 1000u);
    return SwEmcOk;
}

static inline int sw_emc_add_rate(struct sw_emc_control *c, uint32_t khz)
{
    uint32_t i, j;
    for (i = 0; i < c->rate_count && c->rates[i] < khz; ++i) {}
    if (i < c->rate_count && c->rates[i] == khz) return SwEmcOk;
    if (c->rate_count == SW_EMC_MAX_RATES) return SwEmcProtocol;
    for (j = c->rate_count; j > i; --j) c->rates[j] = c->rates[j - 1];
    c->rates[i] = khz;
    ++c->rate_count;
    return SwEmcOk;
}

/* ROUND_RATE may use ceiling, nearest or floor rounding. Find the next
 * transition in its monotonic discrete clock table without assuming ceiling.
 * At most 24 queries per rate over the characterized 1..3200000 kHz range. */
static inline int sw_emc_next_rate(struct sw_emc_control *c, uint32_t current,
                                  uint32_t maximum, uint32_t *next)
{
    uint32_t lo = current + 1u, hi = maximum, rounded;
    int err = sw_emc_round_rate(c, lo, &rounded);
    if (err) return err;
    if (rounded < current || rounded > maximum) return SwEmcProtocol;
    if (rounded > current) { *next = rounded; return SwEmcOk; }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        err = sw_emc_round_rate(c, mid, &rounded);
        if (err) return err;
        if (rounded < current || rounded > maximum) return SwEmcProtocol;
        if (rounded > current) hi = mid;
        else lo = mid + 1u;
    }
    err = sw_emc_round_rate(c, lo, &rounded);
    if (err) return err;
    if (rounded <= current || rounded > maximum) return SwEmcProtocol;
    *next = rounded;
    return SwEmcOk;
}

static inline int sw_emc_requirements(const struct sw_emc_control *c, uint32_t khz,
                                     uint32_t *core_mv, uint32_t *gpu_mv)
{
    /* This controller is T210B01-only (validated firmware/fuse handoff).
     * Switchroot tegra210-dvfs.c at 2d0059fd prefers the installed EMC DVB
     * table over the board/BPMP timing table in init_core_dvfs_table().
     * MRQ32 can return legacy 1100 mV entries on this Mariko firmware; those
     * are neither the B01 core-rail requirements nor trained clock rates.
     * Use the existing process-specific B01 characterization for both rails;
     * BPMP ROUND_RATE still owns which frequencies can actually be selected. */
    *core_mv = sw_emc_core_mv(khz, c->soc_process, c->aggressive_slt);
    *gpu_mv = sw_emc_gpu_mv(khz, c->gpu_process);
    if (!*core_mv || *core_mv > 1050u || !*gpu_mv) return SwEmcProtocol;
    return SwEmcOk;
}

/* Discovery is read-only: it does not select an OC profile or change rails.
 * Clock ID 0 is accepted only after BPMP identifies it as "emc".
 * The active firmware, not 1600 MHz, determines the available upper rate.
 * BPMP may expose a subset of the published characterization table. */
static inline int sw_emc_probe(struct sw_emc_control *c, struct sw_emc_ops ops,
                               void *context, uint32_t soc_process,
                               uint32_t gpu_process, int aggressive_slt)
{
    uint32_t i, request = 14u << 24, value, minimum, maximum, next;
    uint8_t info[113];
    int err;
    memset(c, 0, sizeof(*c));
    if (!ops.message || !ops.raise_floors || !ops.settle_floors ||
        soc_process >= 3 || gpu_process >= 3) return SwEmcInvalid;
    c->ops = ops; c->context = context;
    c->soc_process = soc_process; c->gpu_process = gpu_process;
    c->aggressive_slt = aggressive_slt;
    memset(info, 0, sizeof(info));
    c->probe_stage = 1; /* identify clock */
    err = ops.message(context, SW_EMC_MRQ_CLK, &request, sizeof(request), info, sizeof(info));
    if (err) goto fail;
    if (memcmp(info + 73, "emc", 4)) { err = SwEmcProtocol; goto fail; }
    /* Telemetry remains available even if this BPMP cannot enumerate its
     * trained-rate table. Enumeration failure disables SET_RATE,
     * not a supported read of the independently identified EMC clock. */
    c->probe_stage = 2; /* initial live rate */
    err = sw_emc_read_rate(c, &c->boot_khz);
    if (err) goto fail;
    c->actual_khz = c->boot_khz;
    /* Stages 3/4 belonged to the obsolete MRQ32 voltage gate. B01's DVB
     * characterization is local; do not query the legacy voltage table. */
    c->probe_stage = 5; /* lower clock bound */
    err = sw_emc_round_rate(c, 1u, &minimum);
    if (err) goto fail;
    c->probe_stage = 6; /* upper clock bound */
    err = sw_emc_round_rate(c, SW_EMC_CHARACTERIZED_MAX_KHZ, &maximum);
    if (err) goto fail;
    if (minimum > maximum) { err = SwEmcProtocol; goto fail; }
    /* Query actual firmware-supported steps, not guessed MHz values or the
     * separate voltage table. Ceiling, nearest and floor rounding are valid. */
    c->probe_stage = 7; /* enumerate trained clocks */
    value = minimum;
    for (;;) {
        uint32_t core_mv, gpu_mv;
        err = sw_emc_requirements(c, value, &core_mv, &gpu_mv);
        if (err) goto fail;
        err = sw_emc_add_rate(c, value);
        if (err) goto fail;
        if (value == maximum) break;
        err = sw_emc_next_rate(c, value, maximum, &next);
        if (err) goto fail;
        if (next <= value || next > maximum) { err = SwEmcProtocol; goto fail; }
        value = next;
    }
    c->probe_stage = 8; /* retain the actual boot clock */
    for (i = 0; i < c->rate_count && c->rates[i] != c->boot_khz; ++i) {}
    if (i == c->rate_count) { err = SwEmcProtocol; goto fail; }
    c->ready = 1;
    c->probe_stage = 9;
    return SwEmcOk;
fail:
    c->last_error = err;
    return err;
}

/* Select from real BPMP-supported rates. The caller supplies the memory
 * bandwidth demand and the display/ISO floor, never CPU utilization alone.
 * cap_khz == 0 means the trained maximum. A cap below the display floor is
 * a conflict, not permission to starve scanout. */
static inline uint32_t sw_emc_select(const struct sw_emc_control *c,
                                    uint32_t demand_khz, uint32_t floor_khz,
                                    uint32_t cap_khz)
{
    uint32_t i, maximum = 0, target;
    if (!c->ready || c->fault) return 0;
    for (i = 0; i < c->rate_count; ++i)
        if (!cap_khz || c->rates[i] <= cap_khz) maximum = c->rates[i];
    if (!maximum || maximum < floor_khz) return 0;
    target = demand_khz > floor_khz ? demand_khz : floor_khz;
    if (target > maximum) target = maximum;
    for (i = 0; i < c->rate_count; ++i)
        if (c->rates[i] >= target) return c->rates[i];
    return 0;
}

/* Reach a high target through adjacent trained points. Pre-upshift RING1
 * reservations must fit the CURRENT grant; a direct 204 -> 2500 MHz jump
 * cannot do that. Each step still completes the full rail/QoS/GET_RATE
 * transaction before the next governor sample can request another step. */
static inline uint32_t sw_emc_transition_step(const struct sw_emc_control *c,
                                             uint32_t current, uint32_t target)
{
    uint32_t from = UINT32_MAX, to = UINT32_MAX;
    if (!c || !c->ready || c->fault || !c->rate_count ||
        c->rate_count > SW_EMC_MAX_RATES) return 0;
    for (uint32_t i = 0; i < c->rate_count; ++i) {
        if (i && c->rates[i] <= c->rates[i-1u]) return 0;
        if (c->rates[i] == current) from = i;
        if (c->rates[i] == target) to = i;
    }
    if (from == UINT32_MAX || to == UINT32_MAX) return 0;
    return c->rates[to > from ? from + 1u : to];
}

static inline int sw_emc_set_rate(struct sw_emc_control *c, uint32_t khz)
{
    struct sw_emc_rate_request req;
    uint32_t i, before, after, core_mv, gpu_mv;
    uint64_t reply = 0;
    int err, read_err, clock_submitted = 0;
    if (!c->ready) return SwEmcNotReady;
    if (c->fault) return SwEmcFault;
    if (!c->ops.prepare_qos || !c->ops.verify_qos) return SwEmcNotReady;
    for (i = 0; i < c->rate_count && c->rates[i] != khz; ++i) {}
    if (i == c->rate_count) return SwEmcInvalid;
    c->requested_khz = khz;
    err = sw_emc_read_rate(c, &before);
    if (err) { c->actual_khz = 0; goto fail; }
    c->actual_khz = before;
    err = sw_emc_requirements(c, khz, &core_mv, &gpu_mv);
    if (err) goto fail;
    /* Preserve the old frequency's rail requirements throughout a downshift
     * as well. The initial mode may not yet have an EMC-owned vote. */
    {
        uint32_t old_core, old_gpu;
        err = sw_emc_requirements(c, before, &old_core, &old_gpu);
        if (err) goto fail;
        const uint32_t core = core_mv > old_core ? core_mv : old_core;
        const uint32_t gpu = gpu_mv > old_gpu ? gpu_mv : old_gpu;
        /* A deferred attempt may already have raised a rail (or queued a
         * GPU vote). Preserve that vote through the next attempt too. */
        if (core > c->core_floor_mv) c->core_floor_mv = core;
        if (gpu > c->gpu_floor_mv) c->gpu_floor_mv = gpu;
    }
    err = c->ops.raise_floors(c->context, c->core_floor_mv, c->gpu_floor_mv);
    if (err) goto fail;
    err = c->ops.prepare_qos(c->context, before, khz);
    if (err) goto fail;
    if (before != khz) {
        req.command = (2u << 24) | c->clock_id;
        req.reserved = 0;
        req.hz = (uint64_t)khz * 1000u;
        clock_submitted = 1;
        err = c->ops.message(c->context, SW_EMC_MRQ_CLK, &req, sizeof(req), &reply, sizeof(reply));
        /* A timeout or firmware error does not establish whether the clock
         * changed. Keep both votes, read if transport permits, latch failure. */
        read_err = sw_emc_read_rate(c, &after);
        c->actual_khz = read_err ? 0 : after;
        if (err) goto fail;
        if (read_err) { err = read_err; goto fail; }
        if (after != khz) { err = SwEmcMismatch; goto fail; }
    }
    err = c->ops.verify_qos(c->context, khz);
    if (err) goto fail;
    err = c->ops.settle_floors(c->context, core_mv, gpu_mv);
    if (err) goto fail;
    c->core_floor_mv = core_mv;
    c->gpu_floor_mv = gpu_mv;
    c->actual_khz = khz;
    c->last_error = 0;
    return SwEmcOk;
fail:
    /* Lifecycle/readiness refusal before SET_RATE can be retried once its
     * owner returns. Retain all votes and re-read the real clock on retry.
     * Once SET_RATE was issued, every error remains latched as ambiguous. */
    c->fault = clock_submitted || err != SwEmcNotReady;
    c->last_error = err;
    return err;
}
#endif
