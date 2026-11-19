/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include "../src/t214/emc_control.h"
#include "../src/emc_ioctl.h"

/* Behavioral BPMP/regulator model. No physical register or PMIC access. */
struct fixture {
    uint32_t rate, core_vote, gpu_vote, calls, sets, reads, raises, settles;
    int fail_set, mismatched_read, fail_read, fail_raise, fail_settle;
    int bad_name, floor_round, nearest_round, no_voltage_table, fail_round;
    uint32_t voltage_queries;
    int native_rates;
    int fail_qos_prepare, fail_qos_verify, qos_prepared, qos_verified;
};
static const uint32_t trained[] = {204000u,800000u,1600000u,2133000u,3200000u};
static const uint32_t native_trained[] = {204000u,1331200u,1600000u};
struct legacy_voltage_response {
    uint32_t count;
    struct { uint32_t khz, mv; } pairs[14];
};

static int message(void *context, uint32_t mrq, const void *in, uint32_t in_size,
                   void *out, uint32_t out_size)
{
    struct fixture *f = context;
    const uint32_t *rates = f->native_rates ? native_trained : trained;
    uint32_t rate_count = f->native_rates ? 3u : 5u;
    uint32_t command, op, i;
    ++f->calls;
    if (mrq == 32u) {
        struct legacy_voltage_response *table = out;
        assert(!in_size && out_size == 116u);
        ++f->voltage_queries;
        if (f->no_voltage_table) return -19; /* firmware ENODEV, not a transport fault */
        memset(table, 0, sizeof(*table));
        /* Fan22 native MRQ32 response. It must not gate B01's DVB policy. */
        table->count = 3u;
        for (i = 0; i < 3; ++i) {
            table->pairs[i].khz = native_trained[i];
            table->pairs[i].mv = 1100u;
        }
        return 0;
    }
    assert(mrq == SW_EMC_MRQ_CLK && in_size >= 4);
    memcpy(&command, in, sizeof(command));
    assert((command & 0xffffffu) == 0);
    op = command >> 24;
    if (op == 14) {
        assert(out_size == 113);
        memset(out, 0, out_size);
        memcpy((uint8_t *)out + 73, f->bad_name ? "bad" : "emc", 4);
        return 0;
    }
    assert(out_size == 8);
    if (op == 1) {
        ++f->reads;
        if (f->fail_read) return f->fail_read==2 ? SwEmcNotReady : SwEmcIo;
        *(uint64_t *)out = (uint64_t)f->rate * 1000u;
        return 0;
    }
    assert(in_size == 16u);
    {
        const struct sw_emc_rate_request *req = in;
        uint32_t requested = (uint32_t)(req->hz / 1000u);
        assert(req->reserved == 0 && req->hz % 1000u == 0);
        if (op == 3) {
            uint32_t rounded = rates[rate_count-1u];
            if (f->fail_round) return -19;
            if (f->floor_round) {
                rounded = rates[0];
                for (i = 0; i < rate_count; ++i) if (rates[i] <= requested) rounded = rates[i];
            } else if (f->nearest_round) {
                rounded = rates[0];
                for (i = 1; i < rate_count; ++i) {
                    uint32_t midpoint = rates[i-1] + (rates[i] - rates[i-1]) / 2u;
                    if (requested >= midpoint) rounded = rates[i];
                }
            } else {
                for (i = 0; i < rate_count; ++i) if (rates[i] >= requested) { rounded = rates[i]; break; }
            }
            *(uint64_t *)out = (uint64_t)rounded * 1000u;
            return 0;
        }
        assert(op == 2);
        ++f->sets;
        assert(f->qos_prepared);
        /* Rails must cover BOTH the old and new rate before any clock write. */
        assert(f->core_vote >= sw_emc_core_mv(f->rate, 1, 1));
        assert(f->core_vote >= sw_emc_core_mv(requested, 1, 1));
        assert(f->gpu_vote >= sw_emc_gpu_mv(f->rate, 1));
        assert(f->gpu_vote >= sw_emc_gpu_mv(requested, 1));
        if (!f->mismatched_read) f->rate = requested;
        /* A failed command may still have physically changed the clock. */
        if (f->fail_set) return SwEmcIo;
        *(uint64_t *)out = req->hz;
        return 0;
    }
}
static int raise_floors(void *context, uint32_t core, uint32_t gpu)
{
    struct fixture *f = context;
    ++f->raises;
    if (f->fail_raise==1) return SwEmcIo;
    assert(core >= f->core_vote && gpu >= f->gpu_vote);
    assert(core >= sw_emc_core_mv(f->rate, 1, 1));
    assert(gpu >= sw_emc_gpu_mv(f->rate, 1));
    if (core > f->core_vote) f->core_vote = core;
    if (gpu > f->gpu_vote) f->gpu_vote = gpu;
    if (f->fail_raise==2) return SwEmcNotReady;
    return 0;
}
static int settle_floors(void *context, uint32_t core, uint32_t gpu)
{
    struct fixture *f = context;
    ++f->settles;
    /* Lowering rails before clock completion would fail this assertion. */
    assert(core >= sw_emc_core_mv(f->rate, 1, 1));
    assert(gpu >= sw_emc_gpu_mv(f->rate, 1));
    if (f->fail_settle) return SwEmcIo;
    assert(f->qos_verified);
    f->core_vote = core;
    f->gpu_vote = gpu;
    return 0;
}
static int prepare_qos(void *context, uint32_t before, uint32_t after)
{
    struct fixture *f=context;
    assert(before==f->rate && after);
    f->qos_prepared=!f->fail_qos_prepare; f->qos_verified=0;
    return f->fail_qos_prepare?SwEmcIo:0;
}
static int verify_qos(void *context, uint32_t actual)
{
    struct fixture *f=context;
    assert(actual==f->rate && f->qos_prepared);
    f->qos_verified=!f->fail_qos_verify;
    return f->fail_qos_verify==2?SwEmcNotReady:(f->fail_qos_verify?SwEmcIo:0);
}
static const struct sw_emc_ops ops = {message,raise_floors,settle_floors,prepare_qos,verify_qos};
static void init(struct sw_emc_control *c, struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->rate = 1600000u;
    assert(!sw_emc_probe(c, ops, f, 1, 1, 1));
    assert(c->ready && c->rate_count == 5u);
    assert(c->rates[c->rate_count-1] == 3200000u);
    assert(!f->voltage_queries);
    assert(!f->sets && !f->raises && !f->settles); /* probe has no mutation */
}

int main(void)
{
    struct fixture f;
    struct sw_emc_control c;
    uint32_t calls, khz;
    _Static_assert(sizeof(struct legacy_voltage_response) == 116, "captured BPMP table ABI");
    _Static_assert(sizeof(struct sw_emc_rate_request) == 16, "BPMP clock ABI");
    _Static_assert(offsetof(struct sw_emc_rate_request, hz) == 8, "64-bit clock alignment");
    _Static_assert(sizeof(SWFAN_EMC_STATUS)==188, "Pacecar/Power-State EMC v1 ABI");
    _Static_assert(offsetof(SWFAN_EMC_STATUS,RatesKhz)==32, "EMC rates wire offset");
    _Static_assert(offsetof(SWFAN_EMC_STATUS,ActmonError)==160, "EMC sampler wire offset");
    _Static_assert(offsetof(SWFAN_EMC_STATUS,QosError)==180, "EMC QoS wire offset");
    assert(sw_emc_soc_process(1675u) == 1 && sw_emc_gpu_process(1659u) == 1);
    assert(sw_emc_soc_process(1597u) == 0 && sw_emc_soc_process(1598u) == 1);
    assert(sw_emc_soc_process(1709u) == 2 && sw_emc_gpu_process(1695u) == 2);
    assert(sw_emc_gpu_process(0) == -1 && sw_emc_soc_process(UINT32_MAX) == -1);
    assert(sw_emc_core_mv(3200000, 1, 1) == 950);
    assert(sw_emc_core_mv(3200000, 2, 1) == 900);
    assert(sw_emc_gpu_mv(3200000, 1) == 740);
    assert(sw_emc_gpu_mv(2233001, 1) == 600);
    assert(!sw_emc_core_mv(3200001, 1, 1));
    assert(!sw_emc_gpu_mv(3200000, 3));
    init(&c, &f);
    assert(sw_emc_select(&c, 2500000, 800000, 0) == 3200000);
    assert(sw_emc_select(&c, 100000, 800000, 0) == 800000);
    assert(sw_emc_select(&c, 2500000, 800000, 2133000) == 2133000);
    assert(!sw_emc_select(&c, 2500000, 800000, 204000));
    assert(!sw_emc_set_rate(&c, 3200000));
    assert(f.rate == 3200000 && c.actual_khz == 3200000);
    assert(!sw_emc_set_rate(&c, 800000));
    assert(f.rate == 800000 && c.actual_khz == 800000);
    assert(!sw_emc_set_rate(&c, c.boot_khz));
    assert(c.actual_khz == 1600000); /* explicit recovery, not an upper cap */
    calls = f.calls;
    assert(sw_emc_set_rate(&c, 2666000) == SwEmcInvalid && f.calls == calls);
    init(&c, &f);
    f.fail_set = 1;
    assert(sw_emc_set_rate(&c, 3200000) == SwEmcIo);
    assert(c.fault && c.actual_khz == 3200000 && !f.settles);
    assert(f.core_vote >= 950 && f.gpu_vote >= 740);
    calls = f.calls;
    assert(sw_emc_set_rate(&c, 800000) == SwEmcFault && f.calls == calls);
    init(&c, &f);
    f.mismatched_read = 1;
    assert(sw_emc_set_rate(&c, 3200000) == SwEmcMismatch);
    assert(c.actual_khz == 1600000 && !f.settles);
    init(&c, &f);
    f.fail_read = 1;
    assert(sw_emc_set_rate(&c, 3200000) == SwEmcIo && !c.actual_khz && !f.sets);
    init(&c, &f);
    f.fail_raise = 1;
    assert(sw_emc_set_rate(&c, 3200000) == SwEmcIo && !f.sets);
    init(&c, &f); f.fail_raise=2;
    assert(sw_emc_set_rate(&c,3200000)==SwEmcNotReady && !c.fault && !f.sets && !f.settles);
    assert(c.actual_khz==1600000 && c.core_floor_mv==950 && c.gpu_floor_mv==740);
    assert(f.core_vote==950 && f.gpu_vote==740);
    /* A lower retry retains those raised votes until verified settle, even
     * if no clock change is needed. It must clear the transient error. */
    f.fail_raise=0;
    assert(!sw_emc_set_rate(&c,1600000) && !c.fault && !c.last_error && !f.sets && f.settles==1);
    assert(c.core_floor_mv==650 && c.gpu_floor_mv==590 && f.core_vote==650 && f.gpu_vote==590);
    init(&c, &f); f.fail_read=2;
    assert(sw_emc_set_rate(&c,3200000)==SwEmcNotReady && !c.fault && !c.actual_khz && !f.sets);
    f.fail_read=0;
    assert(!sw_emc_set_rate(&c,800000) && c.actual_khz==800000 && f.sets==1);
    init(&c, &f); f.fail_qos_verify=2;
    assert(sw_emc_set_rate(&c,3200000)==SwEmcNotReady && c.fault && c.actual_khz==3200000 && !f.settles);
    init(&c, &f);
    f.fail_settle = 1;
    assert(sw_emc_set_rate(&c, 3200000) == SwEmcIo && c.actual_khz == 3200000);
    init(&c,&f); f.fail_qos_prepare=1;
    assert(sw_emc_set_rate(&c,3200000)==SwEmcIo && c.fault && !f.sets && !f.settles);
    init(&c,&f); f.fail_qos_verify=1;
    assert(sw_emc_set_rate(&c,3200000)==SwEmcIo && c.fault && f.sets==1 && !f.settles);
    assert(f.core_vote>=950 && f.gpu_vote>=740);
    memset(&f, 0, sizeof(f)); f.rate = 1600000; f.bad_name = 1;
    assert(sw_emc_probe(&c, ops, &f, 1, 1, 1) == SwEmcProtocol);
    f.bad_name = 0; f.native_rates = 1;
    assert(!sw_emc_probe(&c, ops, &f, 1, 1, 1));
    assert(c.ready && c.rate_count == 3 && !f.voltage_queries);
    assert(!memcmp(c.rates, native_trained, sizeof(native_trained)));
    assert(sw_emc_select(&c, 3200000, 204000, 0) == 1600000);
    {
        uint32_t core, gpu;
        assert(!sw_emc_requirements(&c, 1600000, &core, &gpu));
        assert(core == 650 && gpu == 590); /* B01 process1, never legacy1100 */
        assert(sw_emc_requirements(&c, 3200001, &core, &gpu) == SwEmcProtocol);
    }
    assert(!f.sets && !f.raises && !f.settles);
    f.native_rates = 0; f.floor_round = 1;
    assert(!sw_emc_probe(&c, ops, &f, 1, 1, 1) && c.rate_count == 5);
    assert(!memcmp(c.rates, trained, sizeof(trained)));
    f.floor_round = 0; f.nearest_round = 1;
    assert(!sw_emc_probe(&c, ops, &f, 1, 1, 1) && c.rate_count == 5);
    assert(!memcmp(c.rates, trained, sizeof(trained)));
    assert(!f.sets && !f.raises && !f.settles);
    memset(&f, 0, sizeof(f)); f.rate = 1600000; f.no_voltage_table = 1;
    assert(!sw_emc_probe(&c, ops, &f, 1, 1, 1) && c.ready && !f.voltage_queries);
    assert(sw_emc_probe(&c, ops, &f, 3, 1, 1) == SwEmcInvalid);
    f.fail_round = 1;
    assert(sw_emc_probe(&c, ops, &f, 1, 1, 1) == -19);
    assert(!c.ready && c.boot_khz == 1600000u && c.actual_khz == 1600000u);
    f.rate = 2133000;
    assert(!sw_emc_read_rate(&c, &khz) && khz == 2133000u);
    assert(sw_emc_set_rate(&c, 800000u) == SwEmcNotReady && !f.sets);
    puts("PASS: Mariko DVB independent of legacy1100mV/MRQ32, BPMP-only trained clocks, VDD_CORE/GPU ordering and failed/ambiguous transitions (model only)");
    return 0;
}
