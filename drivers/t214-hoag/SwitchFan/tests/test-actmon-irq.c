/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/actmon_irq.h"
#include "../src/t214/emc_governor.h"

struct model {
    uint32_t regs[256];
    uint32_t reject_offset;
    unsigned writes, acks;
};
static uint32_t read32(void *p, unsigned map, uint32_t offset)
{
    struct model *m=p;
    assert(map==SwActmonRegisters && !(offset&3u) && offset<0x400u);
    return m->regs[offset/4u];
}
static void write32(void *p, unsigned map, uint32_t offset, uint32_t value)
{
    struct model *m=p;
    assert(map==SwActmonRegisters && !(offset&3u));
    assert(offset>=0x1c0u && offset<=0x1e4u && offset!=0x1d8u && offset!=0x1dcu && offset!=0x1e0u);
    ++m->writes;
    if (offset==m->reject_offset) return;
    if (offset==0x1e4u) {
        m->regs[offset/4u]&=~value;
        if (!m->regs[offset/4u]) m->regs[0]&=~0x04000000u;
        ++m->acks;
    } else m->regs[offset/4u]=value;
}
static void delay(void *p,uint32_t us) { (void)p;(void)us;assert(!"IRQ code must not delay"); }
static void event(struct model *m,uint32_t flag,uint32_t avg,uint32_t raw)
{
    m->regs[0]|=0x04000000u;
    m->regs[0x1e4u/4u]=flag;
    m->regs[0x1e0u/4u]=avg*20u;
    m->regs[0x1dcu/4u]=raw*20u;
}
int main(void)
{
    struct model m={0};
    struct sw_actmon b={.owned=true,.clock_ready=true};
    struct sw_actmon_irq s={0};
    const struct sw_actmon_io io={&m,read32,write32,delay};
    struct sw_emc_control c={.ready=1,.rate_count=4,.rates={204000,1331200,1600000,2500000}};
    const uint32_t floor=sw_emc_panel_bw_floor(78000u,4u);
    m.regs[0x1c4/4]=11; m.regs[0x1c8/4]=22;
    m.regs[0x1d0/4]=33; m.regs[0x1d4/4]=44;
    assert(!sw_actmon_irq_start(&s,&b,&io,204000,2500000));
    /* Register literals independently derived from T210 CTRL: enable,
     * periodic, K=6, raw up=1 window, raw down=3, all four watermark IRQs. */
    assert(m.regs[0x1c0/4]==0xe1341800u);
    assert(m.regs[0x1c4/4]==2448000 && m.regs[0x1c8/4]==1632000);
    assert(m.regs[0x1d0/4]==4380000 && m.regs[0x1d4/4]==3780000);
    /* Shared line: another monitor neither claims nor modifies MC_ALL. */
    unsigned writes=m.writes;
    m.regs[0]=0x80000000u;
    assert(!sw_actmon_irq_service(&s,&io) && m.writes==writes);
    event(&m,0x80000000u,180000,204000);
    assert(sw_actmon_irq_service(&s,&io) && s.boost_khz==16000 && s.interrupts==1);
    assert(m.regs[0]==0x80000000u && m.regs[0x1e4/4]==0);
    assert(m.regs[0x1d0/4]==3900000 && m.regs[0x1d4/4]==3300000);
    /* Two 20-ms high windows both count even when a passive worker has yet
     * to run. Coalescing a queued work item must not coalesce boost updates. */
    event(&m,0x80000000u,190000,204000);
    assert(sw_actmon_irq_service(&s,&io) && s.boost_khz==48000);
    assert(sw_emc_irq_target(&c,s.average_count/20u,s.boost_khz,floor)==1331200);
    /* Linux clock notifier updates thresholds only to the confirmed rate. */
    assert(!sw_actmon_irq_rate(&s,&io,1331200));
    assert(m.regs[0x1c4/4]==15974400 && m.regs[0x1c8/4]==10649600);
    event(&m,0x40000000u,0,0); /* HW already satisfied three low windows */
    assert(sw_actmon_irq_service(&s,&io) && s.boost_khz==24000);
    assert(sw_emc_irq_target(&c,0,s.boost_khz,floor)==204000);
    /* Average-only IRQs adjust the demand without inventing a raw boost. */
    event(&m,0x01000000u,1000000,1331200);
    assert(sw_actmon_irq_service(&s,&io) && s.boost_khz==24000);
    assert(sw_emc_irq_target(&c,1000000,s.boost_khz,floor)==2500000);
    for(unsigned i=0;i<20;++i){
        event(&m,0x80000000u,1000000,1331200);assert(sw_actmon_irq_service(&s,&io));
    }
    assert(s.boost_khz==2500000 && !(m.regs[0x1c0/4]&0x40000000u));
    event(&m,0x40000000u,0,0);assert(sw_actmon_irq_service(&s,&io));
    assert(s.boost_khz==1250000 && (m.regs[0x1c0/4]&0x40000000u));
    for(unsigned i=0;i<24;++i){
        event(&m,0x40000000u,0,0);assert(sw_actmon_irq_service(&s,&io));
    }
    assert(!s.boost_khz && !(m.regs[0x1c0/4]&0x20000000u));
    /* Up wins if both hardware bits are latched, matching the upstream ISR. */
    event(&m,0xc0000000u,100000,204000);
    assert(sw_actmon_irq_service(&s,&io) && s.boost_khz==16000);
    assert(sw_actmon_irq_restore(&s,&io) && !s.running);
    assert(m.regs[0x1c4/4]==11 && m.regs[0x1c8/4]==22 &&
           m.regs[0x1d0/4]==33 && m.regs[0x1d4/4]==44);
    writes=m.writes;
    event(&m,0x80000000u,100000,204000);
    assert(!sw_actmon_irq_service(&s,&io) && writes==m.writes);

    /* Failed initialization masks the source and still permits restoration. */
    s=(struct sw_actmon_irq){0};m=(struct model){.reject_offset=0x1c8};
    assert(sw_actmon_irq_start(&s,&b,&io,204000,2500000)==SwActmonReadback);
    assert(!s.running && !(m.regs[0x1c0/4]&0x60300000u));
    m.reject_offset=0;assert(sw_actmon_irq_restore(&s,&io));
    s=(struct sw_actmon_irq){0};m=(struct model){0};
    assert(!sw_actmon_irq_start(&s,&b,&io,2500000,2500000));
    event(&m,0x80000000u,2500001,0);
    assert(sw_actmon_irq_service(&s,&io) && s.error==SwActmonReadback && !s.running);
    assert(!(m.regs[0x1c0/4]&0x60300000u));
    /* No made-up OPP, no unsupported display floor, no rounding drift from
     * replacing upstream's integer 166% with floating-point 100/60. */
    c.rates[1]=1660000;c.rates[2]=1666000;
    assert(sw_emc_irq_target(&c,1000000,0,floor)==1660000);
    assert(sw_emc_irq_target(&c,100000,0,floor)==204000);
    assert(!sw_emc_irq_target(&c,2500001,0,floor));
    assert(!sw_emc_irq_target(&c,0,2500001,floor));
    assert(!sw_emc_irq_target(&c,0,0,2500001));
    c.fault=1;assert(!sw_emc_irq_target(&c,1000000,16000,floor));
    puts("PASS: ACTMON IRQ windows/registers, coalesced events, boost/decay, confirmed-clock thresholds, shared IRQ, faults and restoration (model only)");
}
