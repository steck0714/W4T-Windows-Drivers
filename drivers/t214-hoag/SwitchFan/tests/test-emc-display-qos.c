/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/t214/emc_display_qos.h"
#include "../src/t214/emc_ptsa_scale.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); abort(); } } while (0)
static const uint32_t rates[3]={204000,1331200,1600000};
static const uint32_t offsets[SwQosRegisterCount]={
    0x41c,0x420,0x424,0x47c,0x480,0x484,0x960,0x44c,0x50c,
    0x428,0x434,0x494,0x4a0,0x440,0x2e8,0x690,0x2104};
struct fixture {
    uint32_t mc[0x2200/4], grant_shadow, khz;
    unsigned writes, clocks, settles, bootstraps, zero_finishes;
    int firmware, fail_set, fake_bootstrap, scale_not_ready, fail_read, finish_error, post_scale_error;
    uint32_t core_mv,gpu_mv;
    struct sw_emc_control c;
    struct sw_emc_qos q;
};
static uint32_t qr(void *p,enum sw_emc_qos_register r)
{
    struct fixture *f=p;
    if (f->fail_read && r==SwQosGrant) return UINT32_MAX;
    return f->mc[offsets[r]/4u];
}
static void live_valid(struct fixture *f)
{
    uint32_t grant=f->mc[0x960/4], d=f->mc[0x41c/4], ring=f->mc[0x47c/4];
    if (!grant) return; /* Cold reset or explicitly injected firmware fault. */
    uint32_t iso=d+f->mc[0x428/4]+f->mc[0x434/4]+f->mc[0x494/4]+f->mc[0x4a0/4];
    CHECK(d*4u<grant && ring*2u<grant);
    CHECK(ring >= (iso+1u)/2u+f->mc[0x44c/4]+f->mc[0x50c/4]+f->mc[0x440/4]);
    if (d) CHECK((uint64_t)d*f->khz*640u >= (uint64_t)grant*SW_QOS_PANEL_KBPS*11u);
}
static void qw(void *p,enum sw_emc_qos_register r,uint32_t v)
{
    struct fixture *f=p;
    CHECK(r<=SwQosRingMax);
    f->mc[offsets[r]/4u]=v;++f->writes;live_valid(f);
}
static int mr(void *p,uint32_t off,uint32_t *v)
{
    struct fixture *f=p;CHECK(off<sizeof(f->mc) && !(off&3));*v=f->mc[off/4];return 1;
}
static int mw(void *p,uint32_t off,uint32_t v)
{
    struct fixture *f=p;CHECK(off<sizeof(f->mc) && !(off&3));++f->writes;
    if (off==0x960) f->grant_shadow=v;
    else if (off==0xfc) { CHECK(v==1);f->mc[0x960/4]=f->grant_shadow; }
    else f->mc[off/4]=v;
    live_valid(f);return 1;
}
static int bootstrap(void *p,uint32_t current,uint32_t display,uint32_t ring)
{
    struct fixture *f=p;struct sw_emc_ptsa_io io={f,mr,mw,0};++f->bootstraps;
    if (f->fake_bootstrap) return 0;
    return sw_emc_ptsa_bootstrap(&io,current,f->c.rates[f->c.rate_count-1u],display,ring);
}
static int scale(void *p,const struct sw_emc_ptsa_clock *clock,uint32_t ring)
{
    struct fixture *f=p;struct sw_emc_ptsa_io io={f,mr,mw,0};
    if (f->q.pending && f->post_scale_error) return f->post_scale_error;
    if (f->scale_not_ready) return SwEmcNotReady;
    return sw_emc_ptsa_scale(&io,clock,ring);
}
static int finish_zero_clock(void *p,const struct sw_emc_ptsa_clock *clock,uint32_t ring)
{
    struct fixture *f=p;struct sw_emc_ptsa_io io={f,mr,mw,0};
    CHECK(f->q.prepared && f->q.pending && f->q.pending_terms<f->q.budget.count);
    const struct sw_emc_qos_point *target=&f->q.budget.points[f->q.pending_terms];
    CHECK(f->c.actual_khz==target->khz && f->c.requested_khz==target->khz);
    CHECK(clock->grant==target->clock.grant && clock->cpu_read==target->clock.cpu_read &&
          clock->cpu_write==target->clock.cpu_write && ring==target->ring);
    ++f->zero_finishes;
    if (f->finish_error) return f->finish_error;
    return sw_emc_ptsa_finish_zero_clock(&io,clock,ring);
}
static struct sw_emc_qos_io qos_io(struct fixture *f)
{ return (struct sw_emc_qos_io){f,qr,qw,bootstrap,scale,finish_zero_clock}; }
static int prepare(void *p,uint32_t before,uint32_t after)
{
    struct fixture *f=p;struct sw_emc_qos_io io=qos_io(f);
    return sw_emc_qos_prepare(&f->q,&io,f->c.rates,f->c.rate_count,before,after);
}
static int verify(void *p,uint32_t actual)
{
    struct fixture *f=p;struct sw_emc_qos_io io=qos_io(f);
    return sw_emc_qos_verify(&f->q,&io,actual);
}
static int raise(void *p,uint32_t core,uint32_t gpu)
{ struct fixture *f=p;f->core_mv=core;f->gpu_mv=gpu;return 0; }
static int settle(void *p,uint32_t core,uint32_t gpu)
{ struct fixture *f=p;f->core_mv=core;f->gpu_mv=gpu;++f->settles;return 0; }
static void fw_terms(struct fixture *f,uint32_t target)
{
    struct sw_emc_ptsa_clock v;
    CHECK(sw_emc_ptsa_clock_values(target,f->c.rates[f->c.rate_count-1u],SW_EMC_PTSA_POLICY_STANDARD,&v));
    /* Firmware can update children before grant; prepare already supplied
     * enough RING1 margin for all three actual trained frequencies. */
    f->mc[0x44c/4]=v.cpu_read;live_valid(f);
    f->mc[0x50c/4]=v.cpu_write;live_valid(f);
    f->mc[0x960/4]=f->grant_shadow=v.grant;live_valid(f);
}
static int message(void *p,uint32_t mrq,const void *in,uint32_t insz,void *out,uint32_t outsz)
{
    struct fixture *f=p;uint32_t op;memcpy(&op,in,4);op>>=24;
    CHECK(mrq==SW_EMC_MRQ_CLK && outsz==8);
    if (op==1) { uint64_t hz=(uint64_t)f->khz*1000u;memcpy(out,&hz,8);return 0; }
    CHECK(op==2 && insz==sizeof(struct sw_emc_rate_request));
    struct sw_emc_rate_request r;memcpy(&r,in,sizeof(r));
    uint32_t target=(uint32_t)(r.hz/1000u),before=f->khz;
    CHECK(f->core_mv && f->gpu_mv);live_valid(f);++f->clocks;
    if (f->fail_set) return SwEmcIo;
    if (f->firmware==1 && target<before) fw_terms(f,target);
    f->khz=target;live_valid(f);
    if (f->firmware==1 && target>before) fw_terms(f,target);
    /* These are alternative fault hypotheses, not claims about Fan26's
     * actual missing post-clock register tuple. Never silently repair them. */
    if (f->firmware==2) f->mc[0x960/4]=f->grant_shadow=0;
    if (f->firmware==3) f->mc[0x44c/4]=SW_QOS_CPU_READ_MAX+1u;
    if (f->firmware==4) f->mc[0x960/4]=f->grant_shadow=0x400u;
    if (f->firmware>=8 && target==2500000) {
        f->mc[0x44c/4]=277;live_valid(f);
        f->mc[0x50c/4]=31;live_valid(f);
        if (f->firmware==9) ++f->mc[0x50c/4];
        f->mc[0x960/4]=f->grant_shadow=6143;live_valid(f);
    } else if (f->firmware>=12 && target==1331200) {
        /* Fan31 native tuple, with every non-clock field unchanged. */
        f->mc[0x44c/4]=148;live_valid(f);
        f->mc[0x50c/4]=17;live_valid(f);
        f->mc[0x960/4]=f->grant_shadow=3272;live_valid(f);
    } else if (f->firmware==14 && target==1600000) {
        /* Synthetic admissible policy, not a claimed firmware observation. */
        f->mc[0x44c/4]=180;live_valid(f);
        f->mc[0x50c/4]=20;live_valid(f);
        f->mc[0x960/4]=f->grant_shadow=4000;live_valid(f);
    } else if (f->firmware>=10 && target==204000) {
        f->mc[0x44c/4]=23;live_valid(f);
        f->mc[0x50c/4]=3;live_valid(f);
        f->mc[0x960/4]=f->grant_shadow=502;live_valid(f);
        if (f->firmware==11) f->mc[0x50c/4]=0; /* Partial-zero fault. */
        if (f->firmware==15) f->mc[0x50c/4]=40; /* Child exceeds its parent. */
    } else if (f->firmware>=5) {
        /* Exact Fan27 native tuple: the three clock terms zero, parent/DIS
         * unchanged. Cases6/7 violate that contract and must remain errors. */
        f->mc[0x960/4]=f->grant_shadow=f->mc[0x44c/4]=f->mc[0x50c/4]=0;
        if (f->firmware==6) ++f->mc[0x690/4];
        if (f->firmware==7) ++f->mc[0x47c/4];
    }
    memcpy(out,&r.hz,8);return 0;
}
static void cold(struct fixture *f)
{
    struct sw_emc_ptsa_write plan[60];uint32_t count;
    memset(f,0,sizeof(*f));
    CHECK(sw_emc_ptsa_bootstrap_plan(1600000,1600000,16,274,plan,60,&count));
    for (uint32_t i=0;i<count;++i)
        if (plan[i].mask==0x3f) f->mc[plan[i].offset/4]=63;
    f->mc[0x2e8/4]=f->mc[0x690/4]=0x001e001e;
    f->mc[0x2104/4]=0x9160a00d;f->khz=1600000;
    f->c.ops=(struct sw_emc_ops){message,raise,settle,prepare,verify};
    f->c.context=f;f->c.ready=1;f->c.rate_count=3;memcpy(f->c.rates,rates,sizeof(rates));
    f->c.boot_khz=f->c.actual_khz=f->khz;f->c.aggressive_slt=1;
}
int main(void)
{
    const int firmware_behaviors[]={0,1,5,8,10,12,14};
    /* Every pair is reachable through real, individually verified BPMP points.
     * 204 -> 2500 is deliberately unsafe as a direct pre-reserved upshift. */
    const uint32_t noble_rates[4]={204000,1331200,1600000,2500000};
    for (unsigned seed=0;seed<2;++seed)
    for (unsigned behavior=0;behavior<sizeof(firmware_behaviors)/sizeof(firmware_behaviors[0]);++behavior)
        for (unsigned from=0;from<4;++from)
            for (unsigned to=0;to<4;++to) {
                struct fixture f;cold(&f);f.c.rate_count=4;
                memcpy(f.c.rates,noble_rates,sizeof(noble_rates));
                f.khz=f.c.boot_khz=f.c.actual_khz=2500000;
                if (seed) {
                    f.mc[0x960/4]=f.grant_shadow=6143;
                    f.mc[0x44c/4]=277;f.mc[0x50c/4]=31;
                }
                f.firmware=firmware_behaviors[behavior];
                const unsigned ends[2]={from,to};
                for (unsigned leg=0;leg<2;++leg) {
                    unsigned steps=0;
                    do {
                        uint32_t step=sw_emc_transition_step(&f.c,f.khz,noble_rates[ends[leg]]);
                        CHECK(step && ++steps<=4);
                        CHECK(!sw_emc_set_rate(&f.c,step));
                        CHECK(!f.c.fault && !f.q.pending && !verify(&f,step));
                        CHECK(f.core_mv==sw_emc_core_mv(step,f.c.soc_process,1));
                        CHECK(f.gpu_mv==sw_emc_gpu_mv(step,f.c.gpu_process));
                    } while (f.khz!=noble_rates[ends[leg]]);
                }
                CHECK(f.khz==noble_rates[to]);
            }
    {
        struct fixture f;cold(&f);f.c.rate_count=4;
        memcpy(f.c.rates,noble_rates,sizeof(noble_rates));
        f.khz=f.c.boot_khz=f.c.actual_khz=2500000;
        CHECK(!sw_emc_set_rate(&f.c,204000));
        unsigned writes=f.writes,clocks=f.clocks;
        CHECK(sw_emc_set_rate(&f.c,2500000)==SwEmcInvalid);
        CHECK(f.khz==204000 && f.writes==writes && f.clocks==clocks && f.c.fault);
        CHECK(!sw_emc_transition_step(&f.c,204000,2500000));
        f.c.fault=0;
        CHECK(!sw_emc_transition_step(&f.c,205000,2500000));
        CHECK(!sw_emc_transition_step(&f.c,204000,2499000));
        CHECK(!sw_emc_transition_step(NULL,204000,2500000));
    }
    {
        struct fixture f;cold(&f);f.c.rate_count=4;
        memcpy(f.c.rates,noble_rates,sizeof(noble_rates));
        f.khz=f.c.boot_khz=f.c.actual_khz=2500000;
        CHECK(!sw_emc_set_rate(&f.c,1600000));
        f.firmware=9;
        unsigned settles=f.settles;
        /* A different but bounded owned CPU reservation is normalized. */
        CHECK(!sw_emc_set_rate(&f.c,2500000));
        CHECK(!f.c.fault && f.khz==2500000 && f.settles==settles+1u);
        CHECK(!verify(&f,2500000));
    }
    {
        struct fixture f;cold(&f);f.c.rate_count=4;
        memcpy(f.c.rates,noble_rates,sizeof(noble_rates));
        f.khz=f.c.boot_khz=f.c.actual_khz=2500000;f.firmware=11;
        CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcProtocol);
        CHECK(f.c.fault && f.khz==204000 && !f.settles);
        CHECK(!sw_emc_ptsa_mtc_seed(204000,2500000,502,23,3));
    }
    {
        struct fixture f;cold(&f);f.firmware=15;
        CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcMismatch);
        CHECK(f.c.fault && f.khz==204000 && !f.settles);
    }
    {
        struct fixture f;cold(&f);f.c.rate_count=4;
        memcpy(f.c.rates,noble_rates,sizeof(noble_rates));
        f.khz=f.c.boot_khz=f.c.actual_khz=2500000;f.firmware=12;
        f.post_scale_error=SwEmcIo;
        CHECK(sw_emc_set_rate(&f.c,1331200)==SwEmcIo);
        CHECK(f.c.fault && f.q.pending && f.khz==1331200 && !f.settles);
        CHECK(f.core_mv==sw_emc_core_mv(2500000,f.c.soc_process,1));
        CHECK(f.gpu_mv==sw_emc_gpu_mv(2500000,f.c.gpu_process));
    }
    for (unsigned behavior=0;behavior<3;++behavior)
        for (unsigned from=0;from<3u;++from) for (unsigned to=0;to<3u;++to) {
            const int firmware=firmware_behaviors[behavior];
            struct fixture f;cold(&f);f.firmware=firmware;
            CHECK(!sw_emc_set_rate(&f.c,rates[from]));
            unsigned clocks=f.clocks;
            CHECK(!sw_emc_set_rate(&f.c,rates[to]));
            CHECK(f.clocks==clocks+(from!=to) && f.khz==rates[to] && !f.c.fault);
            struct sw_emc_qos_io io=qos_io(&f);
            CHECK(!sw_emc_qos_check(&f.q,&io,rates[to]) && !f.q.pending);
            CHECK(f.zero_finishes==(firmware==5 ? f.clocks : 0));
        }
    for (unsigned behavior=0;behavior<3;++behavior) {
        const int firmware=firmware_behaviors[behavior];
        struct fixture f;cold(&f);f.firmware=firmware;
        CHECK(!sw_emc_set_rate(&f.c,204000));
        CHECK(f.khz==204000 && f.q.display_rate==16 && f.q.ring_rate==53);
        CHECK(f.bootstraps==1 && f.clocks==1 && f.settles==1 && !f.c.fault);
        /* Reattach an already initialized owner at the real low clock. */
        memset(&f.q,0,sizeof(f.q));
        CHECK(!sw_emc_set_rate(&f.c,1331200));CHECK(f.q.ring_rate==232);
        CHECK(!sw_emc_set_rate(&f.c,1600000));CHECK(f.q.ring_rate==274);
        CHECK(!sw_emc_set_rate(&f.c,204000));CHECK(f.q.ring_rate==53);
        CHECK(f.bootstraps==1 && f.clocks==4 && f.settles==4);
        struct sw_emc_qos_io io=qos_io(&f);
        CHECK(!sw_emc_qos_check(&f.q,&io,204000) && !f.q.pending);
    }
    for (unsigned behavior=0;behavior<3;++behavior) {
        const int firmware=firmware_behaviors[behavior];
        struct fixture f;cold(&f);f.firmware=firmware;
        CHECK(!sw_emc_set_rate(&f.c,1600000));
        memset(&f.q,0,sizeof(f.q));
        /* Preserve unexplained parent reservation when adding display demand.
         * Dis=0 / Ring=274 has eight credits beyond its known children; those
         * are not proof of spare credits available to consume for display. */
        f.mc[0x41c/4]=0;
        CHECK(!sw_emc_set_rate(&f.c,204000));
        CHECK(f.q.display_rate==16 && f.q.ring_rate==61);
        CHECK(!sw_emc_set_rate(&f.c,1600000) && f.q.ring_rate==282);
        memset(&f.q,0,sizeof(f.q));
        CHECK(!sw_emc_set_rate(&f.c,204000) && f.q.ring_rate==61);
        CHECK(!sw_emc_set_rate(&f.c,1600000) && f.q.ring_rate==282);

        cold(&f);f.firmware=firmware;
        CHECK(!sw_emc_set_rate(&f.c,1600000));
        memset(&f.q,0,sizeof(f.q));f.mc[0x47c/4]=294;
        CHECK(!sw_emc_set_rate(&f.c,204000));
        CHECK(!sw_emc_set_rate(&f.c,1600000) && f.q.ring_rate==294);
    }
    for (uint32_t parent=337;parent<=342;parent+=5) {
        struct fixture f;cold(&f);CHECK(!sw_emc_set_rate(&f.c,1600000));
        memset(&f.q,0,sizeof(f.q));f.mc[0x47c/4]=parent;
        unsigned writes=f.writes,clocks=f.clocks;
        uint32_t mc_before[sizeof(f.mc)/sizeof(f.mc[0])];memcpy(mc_before,f.mc,sizeof(f.mc));
        /* A pointwise-valid budget is insufficient: adjacent upshifts must also
         * fit the old grant. Refuse before lowering the real clock or writing
         * any register, rather than trapping memory at 204 MHz next time. */
        CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcInvalid);
        CHECK(f.c.fault && !f.q.prepared && f.khz==1600000 && f.c.actual_khz==1600000);
        CHECK(f.writes==writes && f.clocks==clocks && !memcmp(mc_before,f.mc,sizeof(f.mc)));
    }
    {
        struct fixture f;cold(&f);CHECK(!sw_emc_set_rate(&f.c,204000));
        memset(&f.q,0,sizeof(f.q));f.mc[0x47c/4]=274;
        unsigned writes=f.writes,clocks=f.clocks;
        /* Rebind after an interrupted pre-upshift parent write. Do not guess
         * that the unexplained credits belong to the discarded old owner. */
        CHECK(sw_emc_set_rate(&f.c,1600000)==SwEmcInvalid);
        CHECK(!f.q.prepared && f.c.fault && f.khz==204000);
        CHECK(f.writes==writes && f.clocks==clocks && f.mc[0x47c/4]==274);
    }
    for (int firmware=2;firmware<=4;++firmware) {
        struct fixture f;cold(&f);f.firmware=firmware;
        CHECK(sw_emc_set_rate(&f.c,204000)==(firmware==4 ? SwEmcMismatch : SwEmcProtocol));
        CHECK(f.khz==204000 && f.c.actual_khz==204000 && f.c.fault && !f.settles);
        CHECK(f.q.verify_read_error==(firmware==4 ? 0 : SwEmcProtocol));
        CHECK(f.q.verify_readback[SwQosGrant]==f.mc[0x960/4]);
        CHECK(f.q.verify_readback[SwQosCpuRead]==f.mc[0x44c/4]);
        unsigned writes=f.writes;
        CHECK(sw_emc_set_rate(&f.c,1600000)==SwEmcFault && f.writes==writes && f.clocks==1);
        CHECK(f.core_mv && f.gpu_mv); /* No lowering votes on an unknown result. */
    }
    for (int firmware=6;firmware<=7;++firmware) {
        struct fixture f;cold(&f);f.firmware=firmware;
        CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcMismatch && f.c.fault);
        CHECK(!f.zero_finishes && !f.settles && f.khz==204000);
    }
    {
        struct fixture f;cold(&f);f.firmware=5;f.finish_error=SwPtsaIo;
        CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcIo && f.c.fault && !f.settles);
        CHECK(f.zero_finishes==1 && f.q.pending && f.khz==204000);
    }
    {
        struct fixture f;cold(&f);CHECK(!sw_emc_set_rate(&f.c,204000));
        const unsigned writes=f.writes;
        f.mc[0x960/4]=f.grant_shadow=f.mc[0x44c/4]=f.mc[0x50c/4]=0;
        struct sw_emc_qos_io io=qos_io(&f);
        CHECK(sw_emc_qos_verify(&f.q,&io,204000)==SwEmcProtocol);
        CHECK(!f.zero_finishes && f.writes==writes); /* No repair outside SET_RATE. */
    }
    struct fixture f;cold(&f);f.fail_set=1;
    CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcIo && f.khz==1600000 && f.c.fault && !f.settles);
    live_valid(&f); /* Lower prepared terms remain valid at the unchanged clock. */
    cold(&f);f.fake_bootstrap=1;
    CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcProtocol && !f.q.prepared && !f.clocks && !f.writes);
    cold(&f);f.scale_not_ready=1;
    CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcNotReady && !f.c.fault && !f.clocks);
    f.scale_not_ready=0;CHECK(!sw_emc_set_rate(&f.c,204000) && f.bootstraps==1);
    struct sw_emc_qos_io io=qos_io(&f);
    f.fail_read=1;memset(f.q.verify_readback,0xa5,sizeof(f.q.verify_readback));
    CHECK(sw_emc_qos_check(&f.q,&io,204000)==SwEmcIo);
    for (unsigned i=SwQosGrant+1;i<SwQosRegisterCount;++i) CHECK(!f.q.verify_readback[i]);
    f.fail_read=0;f.mc[0x690/4]++;
    CHECK(sw_emc_qos_check(&f.q,&io,204000)==SwEmcMismatch);
    cold(&f);f.mc[0x2104/4]=0;
    CHECK(sw_emc_set_rate(&f.c,204000)==SwEmcProtocol && !f.clocks && !f.writes);
    puts("PASS: actual control/QoS/PTSA chain, observed whole-zero post-clock completion and unknown-result rejection; host model only");
    return 0;
}
