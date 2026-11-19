/* SPDX-License-Identifier: GPL-2.0-only
 * Reference conversion adapted from NVIDIA/CTCaer la_priv.h at
 * 76e6d48970b451c242c20f298b8d63027836bb0b (see emc_ptsa.h).
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../src/t214/emc_ptsa.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#e); abort(); } } while (0)

/* Independent bit-serial reference, preserving the upstream rounding step. */
static uint32_t reference_dda(uint32_t fraction)
{
    uint32_t dda=0, bit, i;
    for (i=0;i<12u;++i) {
        fraction*=2u;
        bit=fraction/10000u;
        dda=(dda<<1)|bit;
        fraction-=bit*10000u;
    }
    if (fraction && dda!=0xfffu) ++dda;
    return dda>0xfffu ? 0xfffu : dda;
}

struct fixture {
    uint32_t r[0x1000/4], original[0x1000/4];
    uint32_t grant_shadow;
    unsigned writes, fail_at;
    int failed, fail_restore, drop_success, latched;
};
static int read_reg(void *p,uint32_t offset,uint32_t *value)
{
    struct fixture *f=p;
    CHECK(offset<sizeof(f->r) && !(offset&3u));
    *value=f->r[offset/4u];
    return 1;
}
static int write_reg(void *p,uint32_t offset,uint32_t value)
{
    struct fixture *f=p;
    CHECK(offset<sizeof(f->r) && !(offset&3u));
    ++f->writes;
    if (!f->failed && f->writes==f->fail_at) {
        f->failed=1;
        if (f->drop_success) return 1; /* transport succeeds, hardware drops write */
        /* A command may have taken effect without becoming persistent data. */
        if (offset==0xfc) { f->latched=1; f->r[0x960/4]=f->grant_shadow; }
        else if (offset==0x960) f->grant_shadow=value;
        else f->r[offset/4u]=value; /* failure may occur after reaching HW */
        return 0;
    }
    if (f->failed && f->fail_restore) return 0;
    if (offset==0xfc) {
        CHECK(value==1u);
        f->latched=1;
        f->r[0x960/4]=f->grant_shadow;
        return 1;
    }
    /* No nonzero client RATE before grant latch, even between write calls. */
    if (offset==0x41c || offset==0x47c || offset==0x44c || offset==0x50c)
        if (value&0xfffu) CHECK(f->latched && (f->r[0x960/4]&0x1fffu));
    if (offset==0x960) f->grant_shadow=value;
    else f->r[offset/4u]=value;
    return 1;
}
static void init_fixture(struct fixture *f)
{
    struct sw_emc_ptsa_write plan[SW_EMC_PTSA_PLAN_CAPACITY];
    uint32_t n=0;
    memset(f,0,sizeof(*f));
    CHECK(sw_emc_ptsa_bootstrap_plan(1600000,1600000,216,571,plan,60,&n));
    for (uint32_t i=0;i<n;++i) {
        if (plan[i].offset==0xfc) continue;
        f->r[plan[i].offset/4u]=0xa5000000u|(plan[i].mask==0x3fu ? 63u : 0u);
    }
    memcpy(f->original,f->r,sizeof(f->r));
    f->grant_shadow=f->r[0x960/4];
}

static void test_transaction(void)
{
    struct fixture f;
    struct sw_emc_ptsa_failure failure;
    struct sw_emc_ptsa_io io={&f,read_reg,write_reg,&failure};
    init_fixture(&f);
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaOk);
    CHECK(f.writes==60 && f.r[0x960/4]==0xa5001206u);
    CHECK(!failure.offset);
    CHECK(f.r[0x41c/4]==0xa50000d8u && f.r[0x44c/4]==0xa50000d0u);
    /* A mixed/existing configuration is not permission to reinitialize MC. */
    f.writes=0;
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaState && !f.writes);
    init_fixture(&f); f.r[0x53c/4]=1; /* preserved GK RATE must also be checked */
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaState && !f.writes);
    init_fixture(&f); f.r[0x2ac/4]=1;
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaState && !f.writes);
    for (unsigned fail=1;fail<=60;++fail) {
        init_fixture(&f); f.fail_at=fail;
        CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaIo);
        /* Timing is a command, so it is deliberately not stored in this model. */
        CHECK(!memcmp(f.original,f.r,sizeof(f.r)));
    }
    init_fixture(&f); f.fail_at=5; f.drop_success=1;
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaMismatch);
    CHECK(failure.offset==0x47c && failure.expected==0xa500023bu && failure.observed==0xa5000000u);
    CHECK(!memcmp(f.original,f.r,sizeof(f.r)));
    /* Dropping grant or its latch must fail before enabling client credits. */
    for (unsigned fail=1;fail<=2;++fail) {
        init_fixture(&f); f.fail_at=fail; f.drop_success=1;
        CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaMismatch);
        CHECK(failure.offset==0x960 && failure.expected==0xa5001206u && failure.observed==0xa5000000u);
        CHECK(!memcmp(f.original,f.r,sizeof(f.r)) && f.grant_shadow==f.original[0x960/4]);
    }
    init_fixture(&f); f.fail_at=5; f.fail_restore=1;
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,1600000,216,571)==SwPtsaRollbackFailed);
}

static void seed_fixture(struct fixture *f)
{
    init_fixture(f);
    f->r[0x960/4]=0xa50017ffu;f->r[0x44c/4]=0xa5000115u;
    f->r[0x50c/4]=0xa500001fu;
    f->grant_shadow=f->r[0x960/4];f->latched=1;
    memcpy(f->original,f->r,sizeof(f->r));
}
static void test_boot_seed(void)
{
    struct fixture f;struct sw_emc_ptsa_io io={&f,read_reg,write_reg,0};
    seed_fixture(&f);
    CHECK(sw_emc_ptsa_bootstrap(&io,2500000,2500000,16,418)==SwPtsaOk);
    CHECK(f.writes==61 && (f.r[0x960/4]&0x1fff)==7209);
    CHECK((f.r[0x44c/4]&0xfff)==325 && (f.r[0x50c/4]&0xfff)==73);
    CHECK((f.r[0x47c/4]&0xfff)==418 && (f.r[0x41c/4]&0xfff)==16);
    for (unsigned fail=1;fail<=61;++fail) {
        seed_fixture(&f);f.fail_at=fail;
        CHECK(sw_emc_ptsa_bootstrap(&io,2500000,2500000,16,418)==SwPtsaIo);
        CHECK(!memcmp(f.original,f.r,sizeof(f.r)) && f.grant_shadow==f.original[0x960/4]);
    }
    const uint32_t altered[]={0x960,0x44c,0x50c,0x47c,0x41c,0x420,0x53c,0x2ac};
    for (unsigned i=0;i<sizeof(altered)/sizeof(altered[0]);++i) {
        seed_fixture(&f);f.r[altered[i]/4]^=1;
        CHECK(sw_emc_ptsa_bootstrap(&io,2500000,2500000,16,418)==SwPtsaState && !f.writes);
    }
    seed_fixture(&f);
    CHECK(sw_emc_ptsa_bootstrap(&io,1600000,2500000,16,418)==SwPtsaState && !f.writes);
    CHECK(sw_emc_ptsa_bootstrap(&io,2500000,3200000,16,418)==SwPtsaState && !f.writes);
}

int main(void)
{
    test_boot_seed();
    struct sw_emc_ptsa_clock c;
    static const struct {
        uint32_t actual, maximum, grant, rd, wr;
    } cases[]={
        /* Real BPMP-discovered clocks, not invented OC training points. */
        {204000,1600000,0x024d,27,6},
        {1331200,1600000,0x0efe,173,39},
        {1600000,1600000,0x1206,208,46},
        /* Characterization of arithmetic only; no hardware claim. */
        {2133000,2133000,0x1807,277,62},
        {3200000,3200000,0x17f7,276,62},
    };
    uint32_t i, khz;
    for (i=0;i<=20000u;++i) CHECK(sw_emc_ptsa_dda(i)==reference_dda(i));
    for (i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        CHECK(sw_emc_ptsa_clock_values(cases[i].actual,cases[i].maximum,SW_EMC_PTSA_POLICY_REWORK,&c));
        CHECK(c.grant==cases[i].grant && c.cpu_read==cases[i].rd && c.cpu_write==cases[i].wr);
    }
    for (khz=25000u;khz<=3200000u;khz+=1000u) {
        CHECK(sw_emc_ptsa_clock_values(khz,3200000u,SW_EMC_PTSA_POLICY_REWORK,&c));
        CHECK(c.grant && c.grant<=0x1fffu && c.cpu_read<=369u && c.cpu_write<=82u);
    }
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(1600000,1331200,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(24999,1600000,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(0,1600000,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(1600000,3200001,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(1600000,UINT32_MAX,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    CHECK(!sw_emc_ptsa_clock_values(1600000,1600000,SW_EMC_PTSA_POLICY_STANDARD,0));
    CHECK(sw_emc_ptsa_clock_values(1600000,2400000,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(c.grant==0x1206 && c.cpu_read==208 && c.cpu_write==46);
    CHECK(sw_emc_ptsa_clock_values(1600000,2400000,SW_EMC_PTSA_POLICY_REWORK,&c));
    CHECK(c.grant==0x0ffa && c.cpu_read==184 && c.cpu_write==41);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(2841000,3200000,SW_EMC_PTSA_POLICY_STANDARD,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    c=(struct sw_emc_ptsa_clock){1,2,3};
    CHECK(!sw_emc_ptsa_clock_values(1600000,1600000,(enum sw_emc_ptsa_policy)2,&c));
    CHECK(!c.grant && !c.cpu_read && !c.cpu_write);
    {
        struct {
            uint32_t before;
            struct sw_emc_ptsa_write w[SW_EMC_PTSA_PLAN_CAPACITY];
            uint32_t after;
        } guarded;
        struct sw_emc_ptsa_write unchanged[SW_EMC_PTSA_PLAN_CAPACITY];
        uint32_t count=1;
        memset(&guarded,0xa5,sizeof(guarded));
        memcpy(unchanged,guarded.w,sizeof(unchanged));
        CHECK(!sw_emc_ptsa_bootstrap_plan(1600000,1600000,216,571,
                                          guarded.w,SW_EMC_PTSA_PLAN_CAPACITY-1u,&count));
        CHECK(!count && !memcmp(guarded.w,unchanged,sizeof(unchanged)));
        CHECK(!sw_emc_ptsa_bootstrap_plan(1600000,1600000,216,373,
                                          guarded.w,SW_EMC_PTSA_PLAN_CAPACITY,&count));
        CHECK(!count && !memcmp(guarded.w,unchanged,sizeof(unchanged)));
        CHECK(sw_emc_ptsa_bootstrap_plan(1600000,1600000,216,571,
                                         guarded.w,SW_EMC_PTSA_PLAN_CAPACITY,&count));
        CHECK(count==60 && guarded.before==0xa5a5a5a5 && guarded.after==0xa5a5a5a5);
        for (i=0;i<count;++i) {
            CHECK(!(guarded.w[i].value&~guarded.w[i].mask));
            for (uint32_t j=0;j<i;++j) CHECK(guarded.w[j].offset!=guarded.w[i].offset);
        }
        CHECK(guarded.w[0].offset==0x960 && guarded.w[0].value==0x1206);
        CHECK(guarded.w[1].offset==0xfc && guarded.w[1].value==1);
        CHECK(guarded.w[2].offset==0x480 && guarded.w[4].offset==0x47c);
        CHECK(guarded.w[count-1].offset==0x41c); /* parent before display child */
    }
    test_transaction();
    puts("PASS: pinned Linux PTSA arithmetic/plan, reset guard and every-write rollback; host MMIO model only");
    return 0;
}
