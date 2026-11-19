/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/actmon.h"
struct model {
    uint32_t regs[0x400/4], source, reset, gate;
    unsigned writes;
    bool ignore_source;
};
static uint32_t read32(void *p, unsigned map, uint32_t offset)
{
    struct model *m=p;
    assert(map<SwActmonMapCount && !(offset&3) && offset+4<=SwActmonResources[map].length);
    const uint32_t address=SwActmonResources[map].address+offset;
    if (address>=0x6000c800u && address<0x6000cc00u) return m->regs[(address-0x6000c800u)/4];
    /* Device addresses from pinned clk.c bank V, independently of the
     * driver's enum or address table: a bank-U mapping must fail here. */
    switch (address) {
    case 0x600063e8u: return m->source;
    case 0x60006358u: return m->reset;
    case 0x60006360u: return m->gate;
    default: assert(!"unexpected ACTMON read address"); return UINT32_MAX;
    }
}
static void write32(void *p, unsigned map, uint32_t offset, uint32_t value)
{
    struct model *m=p; ++m->writes;
    assert(map<SwActmonMapCount && !(offset&3) && offset+4<=SwActmonResources[map].length);
    assert(!SwActmonResources[map].read_only);
    const uint32_t address=SwActmonResources[map].address+offset;
    if (address==0x600063e8u) { if (!m->ignore_source) m->source=value; }
    else if (address==0x60006430u || address==0x60006434u) {
        assert(value==(1u<<(119u%32u)));
        if (address==0x60006430u) { m->reset|=value; memset(m->regs,0,sizeof(m->regs)); }
        else m->reset&=~value;
    } else if (address==0x60006440u || address==0x60006444u) {
        assert(value==(1u<<(119u%32u)));
        if (address==0x60006440u) m->gate|=value; else m->gate&=~value;
    } else {
        assert(address>=0x6000c800u && address<0x6000cc00u);
        offset=address-0x6000c800u;
        assert(offset==4 || offset==SW_ACTMON_MC_ALL || offset==SW_ACTMON_MC_ALL+0x18 ||
               offset==SW_ACTMON_MC_ALL+0xc || offset==SW_ACTMON_MC_ALL+0x24);
        if (offset==SW_ACTMON_MC_ALL+0x24) m->regs[offset/4]&=~value;
        else m->regs[offset/4]=value;
    }
}
static void delay(void *p,uint32_t us) { (void)p; assert(us==5); }
int main(void)
{
    const struct model initial={.source=0x20123456u,.reset=SW_ACTMON_GATE|0x100u,.gate=0x200u};
    struct model m=initial;
    struct sw_actmon s={0};
    const struct sw_actmon_io io={&m,read32,write32,delay};
    uint32_t avg,raw;
    assert(!sw_actmon_start(&s,&io,19200u,1600000u,0));
    assert(s.owned && s.clock_ready && (m.gate&SW_ACTMON_GATE) && !(m.reset&SW_ACTMON_GATE));
    assert((m.source&0xc00000ffu)==0xc0000000u); /* pinned MUX: bits31:30, parent3=clk_m */
    assert((m.source&0x3fffff00u)==(initial.source&0x3fffff00u));
    assert(m.regs[4/4]==19 && m.regs[(SW_ACTMON_MC_ALL+0x18)/4]==1024u);
    assert(m.regs[SW_ACTMON_MC_ALL/4]==SW_ACTMON_CONTROL); /* no watermark IRQs */
    assert(sw_actmon_sample(&s,&io,3200000u,19,&avg,&raw)==SwActmonWaiting);
    m.regs[(SW_ACTMON_MC_ALL+0x20)/4]=400000u*20;
    m.regs[(SW_ACTMON_MC_ALL+0x1c)/4]=1600000u*20;
    assert(!sw_actmon_sample(&s,&io,3200000u,100,&avg,&raw) && avg==400000u && raw==1600000u);
    assert(sw_actmon_sample(&s,&io,3200000u,100,&avg,&raw)==SwActmonWaiting);
    m.regs[(SW_ACTMON_MC_ALL+0x20)/4]=UINT32_MAX;
    assert(sw_actmon_sample(&s,&io,3200000u,200,&avg,&raw)==SwActmonReadback && !avg && !raw);
    assert(sw_actmon_stop(&s,&io) && !s.owned);
    assert(m.source==initial.source && m.reset==initial.reset && m.gate==initial.gate);

    m=initial; m.gate|=SW_ACTMON_GATE; m.reset&=~SW_ACTMON_GATE;
    m.source=SW_ACTMON_CLK_M; m.regs[1]=19;
    m.regs[SW_ACTMON_MC_ALL/4]=SW_ACTMON_ENABLE;
    assert(sw_actmon_start(&s,&io,19200,1600000,0)==SwActmonBusy && !s.owned && !m.writes);
    m.regs[SW_ACTMON_MC_ALL/4]=0x20000000u; m.regs[(SW_ACTMON_MC_ALL+0x18)/4]=123;
    m.regs[0x40/4]=0xabcdefu; /* another monitor is not ours */
    assert(!sw_actmon_start(&s,&io,19200,1600000,0) && s.borrowed_clock);
    assert(sw_actmon_stop(&s,&io));
    assert(m.regs[SW_ACTMON_MC_ALL/4]==0x20000000u && m.regs[(SW_ACTMON_MC_ALL+0x18)/4]==123);
    assert(m.regs[0x40/4]==0xabcdefu && m.source==SW_ACTMON_CLK_M && m.regs[1]==19);

    m=initial; m.ignore_source=true;
    assert(sw_actmon_start(&s,&io,19200,1600000,0)==SwActmonReadback && s.owned);
    assert(sw_actmon_stop(&s,&io) && m.reset==initial.reset && m.gate==initial.gate);
    m=initial;
    assert(sw_actmon_start(&s,&io,38400,1600000,0)==SwActmonInvalid && !m.writes);
    puts("PASS: MC_ALL ownership, period/clock setup, real counter samples, invalid/stale samples and partial-init unwind (model only)");
    return 0;
}
