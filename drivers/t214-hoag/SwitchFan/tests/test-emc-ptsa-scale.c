/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/t214/emc_ptsa_scale.h"
#include "../src/t214/emc_qos_budget.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); abort(); } } while (0)
struct hw {
    uint32_t live[4], shadow[4], initial[4];
    uint32_t display, ring2;
    unsigned writes, fail_at, high_priority_violation;
    int failed, drop, fail_restore, immediate_cpu;
};
static void check_live(struct hw *s)
{
    if (!s->live[0] && !s->live[1] && !s->live[3]) return; /* Observed firmware tuple. */
    if (!sw_ptsa_scale_budget_valid(s->live,s->display,0,s->ring2))
        ++s->high_priority_violation;
}
static int rd(void *p,uint32_t off,uint32_t *v)
{
    struct hw *s=p;
    for (unsigned i=0;i<4;++i) if (sw_ptsa_scale_offsets[i]==off) { *v=s->live[i]; return 1; }
    if (off==0x41c) { *v=s->display; return 1; }
    if (off==0x440) { *v=s->ring2; return 1; }
    if (off==0x428 || off==0x434 || off==0x494 || off==0x4a0) { *v=0; return 1; }
    return 0;
}
static int wr(void *p,uint32_t off,uint32_t v)
{
    struct hw *s=p;
    int fail=0;
    ++s->writes;
    if (s->failed && s->fail_restore) return 0;
    if (!s->failed && s->writes==s->fail_at) {
        s->failed=1; fail=1;
        if (s->drop) return 1;
    }
    if (off==0xfc) {
        CHECK(v==1);
        memcpy(s->live,s->shadow,sizeof(s->live));
    } else {
        unsigned i;
        for (i=0;i<4 && sw_ptsa_scale_offsets[i]!=off;++i) {}
        CHECK(i<4);
        s->shadow[i]=v;
        if (i==2 || (s->immediate_cpu && i<2)) s->live[i]=v;
    }
    check_live(s);
    return !fail; /* Failed writes can have reached the register. */
}
static void init(struct hw *s,const struct sw_emc_qos_point *p,int immediate)
{
    memset(s,0,sizeof(*s));
    s->live[0]=0xa5000000u|p->clock.cpu_read;
    s->live[1]=0xa5000000u|p->clock.cpu_write;
    s->live[2]=0xa5000000u|p->ring;
    s->live[3]=0xa5000000u|p->clock.grant;
    memcpy(s->shadow,s->live,sizeof(s->live));
    memcpy(s->initial,s->live,sizeof(s->live));
    s->display=16; s->ring2=12; s->immediate_cpu=immediate;
}
int main(void)
{
    const uint32_t rates[]={204000,1331200,1600000};
    struct sw_emc_qos_budget b;
    CHECK(sw_emc_qos_budget_plan(rates,3,SW_EMC_PTSA_POLICY_STANDARD,312000,0,0,12,0,&b));
    for (int immediate=0;immediate<=1;++immediate) {
        for (unsigned from=0;from<3;++from) for (unsigned to=0;to<3;++to) {
            struct hw s;
            struct sw_emc_ptsa_failure failure;
            struct sw_emc_ptsa_io io={&s,rd,wr,&failure};
            init(&s,&b.points[from],immediate);
            CHECK(!sw_emc_ptsa_scale(&io,&b.points[to].clock,b.points[to].ring));
            CHECK(!s.high_priority_violation && !failure.offset);
            CHECK((s.live[3]&0x1fff)==b.points[to].clock.grant);
            CHECK((s.live[2]&0xfff)==b.points[to].ring);
            CHECK((s.live[0]&0xfff)==b.points[to].clock.cpu_read);
            CHECK((s.live[1]&0xfff)==b.points[to].clock.cpu_write);
            if (from==to) { CHECK(!s.writes); continue; }
            CHECK(s.writes==6);
            for (unsigned fail=1;fail<=6;++fail) for (int drop=0;drop<=1;++drop) {
                init(&s,&b.points[from],immediate); s.fail_at=fail; s.drop=drop;
                int result=sw_emc_ptsa_scale(&io,&b.points[to].clock,b.points[to].ring);
                /* A dropped redundant latch may have no remaining effect.
                 * Success still requires every active and shadow value to
                 * equal the requested tuple, not a particular write count. */
                if (result==SwPtsaOk) {
                    const uint32_t wanted[4]={
                        0xa5000000u|b.points[to].clock.cpu_read,
                        0xa5000000u|b.points[to].clock.cpu_write,
                        0xa5000000u|b.points[to].ring,
                        0xa5000000u|b.points[to].clock.grant};
                    CHECK(drop && !memcmp(s.live,wanted,sizeof(wanted)) &&
                          !memcmp(s.shadow,wanted,sizeof(wanted)));
                } else {
                    CHECK(result==(drop ? SwPtsaMismatch : SwPtsaIo));
                    CHECK(!memcmp(s.live,s.initial,sizeof(s.live)));
                    CHECK(!memcmp(s.shadow,s.initial,sizeof(s.shadow)));
                }
                CHECK(!s.high_priority_violation);
            }
            init(&s,&b.points[from],immediate); s.fail_at=4; s.fail_restore=1;
            CHECK(sw_emc_ptsa_scale(&io,&b.points[to].clock,b.points[to].ring)==SwPtsaRollbackFailed);
        }
    }
    for (int immediate=0;immediate<=1;++immediate) for (unsigned point=0;point<3;++point) {
        for (unsigned fail=0;fail<=5;++fail) for (int drop=0;drop<=1;++drop) {
            struct hw s;struct sw_emc_ptsa_failure failure;
            struct sw_emc_ptsa_io io={&s,rd,wr,&failure};
            init(&s,&b.points[point],immediate);
            s.live[0]=s.live[1]=s.live[3]=0;
            memcpy(s.shadow,s.live,sizeof(s.live));memcpy(s.initial,s.live,sizeof(s.live));
            s.fail_at=fail;s.drop=drop;
            int result=sw_emc_ptsa_finish_zero_clock(&io,&b.points[point].clock,b.points[point].ring);
            if (!result) {
                const uint32_t wanted[4]={b.points[point].clock.cpu_read,b.points[point].clock.cpu_write,
                    0xa5000000u|b.points[point].ring,b.points[point].clock.grant};
                CHECK(!fail || drop);
                CHECK(!memcmp(s.live,wanted,sizeof(wanted)) && !memcmp(s.shadow,wanted,sizeof(wanted)));
            } else {
                CHECK(fail && result==(drop ? SwPtsaMismatch : SwPtsaIo));
                CHECK(!memcmp(s.live,s.initial,sizeof(s.live)) && !memcmp(s.shadow,s.initial,sizeof(s.shadow)));
            }
            CHECK(!s.high_priority_violation && s.live[2]==s.initial[2]);
        }
    }
    struct hw s;struct sw_emc_ptsa_io io={&s,rd,wr,0};
    init(&s,&b.points[0],0);s.live[0]=s.live[1]=s.live[3]=0;
    memcpy(s.shadow,s.live,sizeof(s.live));s.fail_at=3;s.fail_restore=1;
    CHECK(sw_emc_ptsa_finish_zero_clock(&io,&b.points[0].clock,b.points[0].ring)==SwPtsaRollbackFailed);
    init(&s,&b.points[0],0);s.live[3]=0;
    CHECK(sw_emc_ptsa_finish_zero_clock(&io,&b.points[0].clock,b.points[0].ring)==SwPtsaState && !s.writes);
    init(&s,&b.points[0],0);s.live[3]=s.shadow[3]=0;
    CHECK(sw_emc_ptsa_scale(&io,&b.points[2].clock,b.points[2].ring)==SwPtsaState && !s.writes);
    init(&s,&b.points[2],0);s.display=216;
    s.live[2]=s.shadow[2]=0xa5000000u|571u;
    CHECK(sw_emc_ptsa_scale(&io,&b.points[0].clock,b.points[0].ring)==SwPtsaInvalid && !s.writes);
    puts("PASS: per-rate PTSA ordering, every partial failure, shadow/immediate CPU terms; no real clock operation");
    return 0;
}
