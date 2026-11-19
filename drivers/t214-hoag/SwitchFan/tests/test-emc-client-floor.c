/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/emc_client_floor.h"
int main(void)
{
    struct sw_emc_control c={.ready=1,.rate_count=4,.rates={204000,1331200,1600000,2500000}};
    const uint32_t cpu[]={102000,204800,307200,403000,710400,921600,1581000};
    const uint32_t floor[]={68000,102000,204000,408000,665600,1600000,2500000};
    for(unsigned i=0;i<7;++i){
        assert(sw_emc_cpu_floor_khz(cpu[i],2500000)==floor[i]);
        assert(sw_emc_cpu_floor_khz(cpu[i]-1,2500000)==(i?floor[i-1]:0));
    }
    /* A cache-resident CPU workload still requests memory bandwidth, exactly
     * as cpu_emc.c does; low ACTMON utilization cannot erase this client. */
    assert(sw_emc_client_target(&c,204000,102000,38304)==204000);
    assert(sw_emc_client_target(&c,204000,403000,38304)==1331200);
    assert(sw_emc_client_target(&c,204000,921600,38304)==1600000);
    assert(sw_emc_client_target(&c,204000,1581000,38304)==2500000);
    assert(sw_emc_client_target(&c,204000,2397000,38304)==2500000);
    /* Other clients retain their higher votes when CPU drops back to idle. */
    assert(sw_emc_client_target(&c,2500000,102000,38304)==2500000);
    assert(sw_emc_client_target(&c,204000,102000,1700000)==2500000);
    /* Repeat a workload start/stop; no stale high floor survives the client. */
    for(unsigned i=0;i<100;++i){
        assert(sw_emc_client_target(&c,204000,1581000,38304)==2500000);
        assert(sw_emc_client_target(&c,204000,102000,38304)==204000);
    }
    c.rate_count=3;
    assert(sw_emc_client_target(&c,204000,2397000,38304)==1600000);
    assert(!sw_emc_client_target(&c,2500000,102000,38304));
    c.fault=1;assert(!sw_emc_client_target(&c,204000,2397000,38304));
    puts("PASS: captured Noble CPU/EMC thresholds, client aggregation, trained-cap rounding and fault rejection (host model only)");
}
