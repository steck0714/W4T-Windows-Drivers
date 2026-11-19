/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/emc_governor.h"
int main(void)
{
    struct sw_emc_control c={.ready=1,.rate_count=4,.rates={204000,1331200,1600000,2500000}};
    uint32_t floor=sw_emc_panel_bw_floor(78000,4);
    assert(floor==38304u);
    assert(!sw_emc_panel_bw_floor(0,4) && !sw_emc_panel_bw_floor(UINT32_MAX,4));
    assert(sw_emc_irq_target(&c,0,0,floor)==204000);
    assert(sw_emc_irq_target(&c,120000,16000,floor)==1331200);
    assert(sw_emc_irq_target(&c,900000,0,floor)==1600000);
    assert(sw_emc_irq_target(&c,1000000,0,floor)==2500000);
    assert(sw_emc_irq_target(&c,2500000,2500000,floor)==2500000);
    /* IRQ-free telemetry repeats never accumulate a software boost. */
    for (unsigned i=0;i<1000;++i)
        assert(sw_emc_irq_target(&c,120000,16000,floor)==1331200);
    assert(sw_emc_irq_target(&c,0,0,1700000)==2500000);
    assert(!sw_emc_irq_target(&c,0,0,2500001));
    assert(!sw_emc_irq_target(&c,UINT32_MAX,0,floor));
    assert(!sw_emc_irq_target(&c,0,UINT32_MAX,floor));
    c.fault=1;assert(!sw_emc_irq_target(&c,1000000,0,floor));
    c.fault=0;c.ready=0;assert(!sw_emc_irq_target(&c,1000000,0,floor));
    c.ready=1;c.rate_count=1;
    assert(!sw_emc_irq_target(&c,0,0,204001));
    assert(!sw_emc_runtime_floor(0));
    puts("PASS: IRQ demand, trained OPP rounding, floor204, read-only telemetry and invalid/fault rejection (model only)");
}
