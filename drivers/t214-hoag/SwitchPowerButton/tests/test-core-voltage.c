/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../src/core_voltage_control.h"

struct model {
    uint8_t reg;
    unsigned reads, writes, settles, fail_read;
    bool fail_write;
};
static bool read_reg(void *p, uint8_t *out)
{
    struct model *m = p;
    if (++m->reads == m->fail_read) return false;
    *out = m->reg;
    return true;
}
static bool write_reg(void *p, uint8_t value)
{
    struct model *m = p;
    ++m->writes;
    if (m->fail_write) return false;
    m->reg = value;
    return true;
}
static void settle(void *p) { ++((struct model *)p)->settles; }
int main(void)
{
    struct model m = {.reg = 0xc8}; /* 700 mV, upper SD0 bits owned elsewhere */
    struct sw_core_voltage state = {0};
    const struct sw_core_io io = {&m, read_reg, write_reg, settle};
    assert(sw_core_voltage_vote(&state,&io,false,0));
    assert(state.boot_uv == 700000u && state.applied_uv == 700000u && !m.writes);
    assert(sw_core_voltage_vote(&state,&io,true,712001u));
    assert(m.reg == 0xc9u && state.applied_uv == 712500u && state.vote_uv == 712001u);
    assert(sw_core_voltage_vote(&state,&io,true,600000u));
    assert(m.reg == 0xc8u && state.applied_uv == 700000u); /* other clients' floor retained */
    assert(sw_core_voltage_vote(&state,&io,true,950000u));
    unsigned writes = m.writes;
    assert(!sw_core_voltage_vote(&state,&io,true,1050001u) && m.writes == writes);
    m.fail_write = true;
    assert(!sw_core_voltage_vote(&state,&io,true,1000000u));
    assert(state.vote_uv == 1000000u && state.applied_uv == 950000u);
    m.fail_write = false;
    m.fail_read = m.reads + 2u; /* a down-write succeeded, its readback is unknown */
    unsigned settled = m.settles;
    assert(!sw_core_voltage_vote(&state,&io,true,700000u));
    assert(state.vote_uv == 1000000u && !state.applied_uv && m.settles == settled);
    m.fail_read = 0;
    assert(sw_core_voltage_vote(&state,&io,true,1050000u));
    assert(state.applied_uv == 1050000u && (m.reg & 0xc0u) == 0xc0u);
    assert(sw_core_voltage_vote(&state,&io,true,0));
    assert(state.applied_uv == 700000u && !state.vote_uv);
    puts("PASS: MAX77620 core votes, voltage rounding, retained client floor and failed/unknown PMIC results (model only)");
    return 0;
}
