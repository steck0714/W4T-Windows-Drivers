/* SPDX-License-Identifier: GPL-2.0-only
 * DFLL state/ownership/timeout controls with simulated MMIO and PMIC.
 * Not a CPU frequency, voltage or physical-stability test.
 */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/dfll_control.h"

static uint32_t regs[0x300 / 4], parent, uv, minimum_restore_uv;
static bool boot_pllx_ready, bad_counter, fail_select_after;
static unsigned mux_switches, voltage_writes;

static void settle(void)
{
    uint32_t cfg = regs[SW_DFLL_OUTPUT_CFG / 4], force = regs[SW_DFLL_FORCE / 4];
    uint32_t lo = (cfg >> 8) & 63u, hi = (cfg >> 16) & 63u;
    int32_t index;
    if (!(cfg & SW_DFLL_I2C_ENABLE)) return;
    if (force & 0x40u) index = force & 63u;
    else if (regs[SW_DFLL_CTRL / 4] == 2u) {
        int32_t f = (regs[SW_DFLL_FREQ_REQ / 4] >> 16) & 0xfffu;
        if (f & 0x800) f -= 0x1000;
        index = (int32_t)((cfg >> 24) & 63u) + f / 64;
        if (index < (int32_t)lo) index = lo;
        if (index > (int32_t)hi) index = hi;
    } else return;
    assert(index >= 0 && index < 33);
    /* The CPU must already be on DFLL before it can take over M4. */
    assert(parent == 2u || (boot_pllx_ready && (force & 0x40u)));
    uv = SwDfllCodeUv((uint8_t)regs[(SW_DFLL_LUT + (uint32_t)index * 4u) / 4]);
    regs[SW_DFLL_I2C_STS / 4] = (uint32_t)index << 1;
    ++voltage_writes;
}
static uint32_t read_reg(void *ctx, uint32_t offset)
{
    (void)ctx;
    assert(offset < sizeof(regs) && !(offset & 3u));
    if (offset == SW_DFLL_MON_DATA)
        return (1u << 16) | (bad_counter ? 1u : (regs[SW_DFLL_FREQ_REQ / 4] & 127u) * 2u);
    return regs[offset / 4];
}
static void write_reg(void *ctx, uint32_t offset, uint32_t value)
{
    (void)ctx;
    assert(offset < sizeof(regs) && !(offset & 3u));
    if (offset == SW_DFLL_CTRL && value == 0u) assert(parent == 1u);
    regs[offset / 4] = offset == SW_DFLL_INTR_STS ? 0u : value;
    settle();
}
static void delay_us(void *ctx, uint32_t time) { (void)ctx; (void)time; }
static bool read_uv(void *ctx, uint32_t *out) { (void)ctx; *out = uv; return true; }
static uint32_t cpu_parent(void *ctx) { (void)ctx; return parent; }
static bool prepare_pllx(void *ctx)
{ (void)ctx; assert(parent == 2u); boot_pllx_ready = true; return true; }
static bool select_parent(void *ctx, bool dfll)
{
    (void)ctx;
    if (dfll) assert(regs[SW_DFLL_CTRL / 4] == 1u && !(regs[SW_DFLL_OUTPUT_CFG / 4] & SW_DFLL_I2C_ENABLE));
    else assert(boot_pllx_ready && uv >= minimum_restore_uv);
    parent = dfll ? 2u : 1u; ++mux_switches;
    return !(dfll && fail_select_after);
}
static const struct sw_dfll_io io = {NULL,read_reg,write_reg,delay_us,read_uv,cpu_parent,prepare_pllx,select_parent};
static void reset(struct sw_dfll_control *s)
{
    memset(s, 0, sizeof(*s)); memset(regs, 0, sizeof(regs)); parent = 1; uv = 1120000;
    boot_pllx_ready = bad_counter = fail_select_after = false;
    minimum_restore_uv = 800000u;
    mux_switches = voltage_writes = 0;
}
int main(void)
{
    struct sw_dfll_control s;
    reset(&s);
    assert(SwDfllPrepare(&io,&s,1630,0,1600000,0x33,81600,800000));
    assert(!voltage_writes && !mux_switches && parent == 1u);
    assert(regs[SW_DFLL_I2C_CFG / 4] == 0x00107066 && regs[SW_DFLL_I2C_REG / 4] == 0x26);
    assert(regs[SW_DFLL_I2C_DIV / 4] == 0x00190001);
    assert(SwDfllStart(&io,&s,1963500,40));
    assert(s.active && s.high_tune && s.measured_khz == 1963500 && parent == 2u);
    assert(SwDfllSetRate(&io,&s,2397000,40) && s.measured_khz == 2397000);
    assert(SwDfllSetRate(&io,&s,2397000,85) && s.thermally_limited && s.requested_khz == 1963500);
    assert(SwDfllSetRate(&io,&s,102000,40) && !s.high_tune && s.measured_khz == 102000);
    s.emc_floor_uv = 620000u;
    memset(s.floor_khz, 0, sizeof(s.floor_khz));
    memset(s.high_floor_khz, 0, sizeof(s.high_floor_khz));
    assert(SwDfllSetRate(&io,&s,102000,40) && uv >= 620000u);
    assert(SwDfllStop(&io,&s) && !s.active && !s.prepared && parent == 1u);
    assert(!(regs[SW_DFLL_OUTPUT_CFG / 4] & SW_DFLL_I2C_ENABLE) && uv >= 800000u);

    /* RAM may still be at a high rate across a driver rebind. Its floor
     * exceeds the old CPU boot voltage and must survive start and stop. */
    reset(&s); minimum_restore_uv=620000u;
    assert(SwDfllPrepare(&io,&s,1630,0,1600000,0x33,81600,600000));
    s.emc_floor_uv=620000u;
    assert(SwDfllStart(&io,&s,102000,40) && uv>=620000u);
    assert(SwDfllStop(&io,&s) && parent==1u && uv>=620000u);

    reset(&s);
    assert(SwDfllPrepare(&io,&s,1630,0,1600000,0x33,81600,800000));
    bad_counter = true;
    assert(!SwDfllStart(&io,&s,1963500,40) && parent == 1u && !s.active);
    reset(&s);
    assert(SwDfllPrepare(&io,&s,1630,0,1600000,0x33,81600,800000));
    fail_select_after = true;
    assert(!SwDfllStart(&io,&s,1963500,40) && parent == 1u && !s.active);
    reset(&s); parent = 2;
    assert(!SwDfllPrepare(&io,&s,1630,0,1600000,0x33,81600,800000) && !voltage_writes);
    puts("PASS: DFLL ownership, real-readback completion requirements, OC/thermal requests, boot-PLLX return and partial-switch/timeout recovery (model only)");
    return 0;
}
