/* SPDX-License-Identifier: MIT */
#include "../platform/t210_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t registers[0x140000 / 4];
static bool counter_bad, no_lock, slowdown_stuck, pmic_fail;
static uint32_t writes, target_khz;
static uint8_t vout, dvs;
static void forbid(uint32_t off)
{
    /* Legacy GPCPLL must never touch the NA/DVFS registers (C1-only). */
    assert(off != 0x137010 && off != 0x137014 && off != 0x13700c &&
           off != 0x137018 && off != 0x132820 && off != 0x021944 && off != 0x021948);
}
static uint32_t read_reg(void *ctx, uint32_t address)
{
    (void)ctx;
    assert(address >= 0x57000000 && address < 0x57140000);
    return registers[(address - 0x57000000) / 4];
}
static void write_reg(void *ctx, uint32_t address, uint32_t value)
{
    uint32_t off = address - 0x57000000;
    (void)ctx;
    assert(address >= 0x57000000 && address < 0x57140000);
    forbid(off);
    ++writes;
    if (off == 0x020160 && slowdown_stuck && !(value & 0x003f0000)) return;
    registers[off / 4] = value;
    if (off == 0x137000) {
        /* LOCK (bit 17) follows ENABLE with a valid coefficient. */
        if ((value & 1) && !no_lock && !(value & 0x10)) registers[off / 4] |= 1u << 17;
        else registers[off / 4] &= ~(1u << 17);
    }
    if (off == 0x134124 && value == 0x00110320) {
        uint32_t coeff = registers[0x137004 / 4] & 0x3fffff;
        uint32_t n = (coeff >> 8) & 0xff, pl = coeff >> 16, m = coeff & 0xff;
        /* PLL must be enabled, locked, synced and selected before counting. */
        assert((registers[0x137000 / 4] & 0x20007) == 0x20005);
        assert(registers[0x137100 / 4] & 1);
        assert(!(registers[0x20160 / 4] & 0x003f0000));
        assert(m == 1 && 38400u * n / pl / 2 == target_khz);
        assert(38400u * n >= 1300000u && 38400u * n <= 2600000u);
        registers[0x134128 / 4] = counter_bad ? 3200 : target_khz / 48;
    }
}
static bool read_pmic(void *ctx, uint8_t address, uint8_t reg, uint8_t *value)
{
    (void)ctx; assert(address == 0x1c);
    *value = reg == 0 ? vout : dvs;
    return true;
}
static bool write_pmic(void *ctx, uint8_t addr, uint8_t reg, uint8_t value)
{
    (void)ctx; assert(addr == 0x1c && (reg == 0 || reg == 1));
    assert(!(registers[0x137000 / 4] & 1)); /* voltage before PLL start */
    if (pmic_fail) return false;
    if (reg == 0) vout = value; else dvs = value;
    return true;
}
static void delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static void reset(void)
{
    memset(registers, 0, sizeof(registers));
    registers[0] = 0x12b000a1;           /* Erista PMC_BOOT_0 */
    registers[0x137000 / 4] = 2;         /* IDDQ, PLL off */
    registers[0x20798 / 4] = 0x105;
    registers[0x20160 / 4] = 0x00220000;
    vout = dvs = 0x80 | 31;
    target_khz = TG_GPU_FIXED_KHZ;
    counter_bad = no_lock = slowdown_stuck = pmic_fail = false;
    writes = 0;
}
int main(void)
{
    struct tg_gpu_power p = {.owned = true, .ready = true, .pmic_address = 0x1c};
    struct tg_gpu_power_io io = {NULL, read_reg, write_reg, read_pmic, write_pmic, delay};
    struct tg_gpu_clock c = {0};
    assert(tg_gpu_clock_required_uv(0, TG_GPU_FIXED_KHZ) == UINT32_MAX);
    assert(tg_gpu_clock_required_uv(1400, 1234567) == UINT32_MAX);
    assert(tg_gpu_clock_required_uv(1400, TG_GPU_FIXED_KHZ) == 1000000);
    assert(tg_gpu_clock_required_uv(1400, TG_GPU_OVERCLOCK_KHZ) == 1100000);
    reset();
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_OK);
    assert(c.measured_khz == 460800 && c.counter == 9600 && c.stage == 5);
    assert(c.rail_uv == 1000000 && (vout & 0x7f) == 63 && (dvs & 0x7f) == 63);
    assert(registers[0x20798 / 4] == 0x105);
    assert(registers[0x20160 / 4] == 0); /* persists beyond the counter sample */
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    assert(!c.owned && !c.running && registers[0x137000 / 4] == 2);
    assert(registers[0x20160 / 4] == 0x00220000 && !(registers[0x137100 / 4] & 1));
    reset(); memset(&c, 0, sizeof(c)); slowdown_stuck = true;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_READBACK_ERROR);
    assert(c.owned && !c.running);
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    assert(registers[0x20160 / 4] == 0x00220000);
    reset(); memset(&c, 0, sizeof(c)); counter_bad = true;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_COUNTER_ERROR);
    assert(c.owned);
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    reset(); memset(&c, 0, sizeof(c)); no_lock = true;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_CALIBRATION_ERROR);
    assert(!(registers[0x137100 / 4] & 1)); /* never switched to an unlocked VCO */
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    assert(registers[0x137000 / 4] == 2);
    reset(); memset(&c, 0, sizeof(c)); registers[0x137000 / 4] = 2 | 0x10; /* lock detector off */
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_OK);
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    reset(); memset(&c, 0, sizeof(c)); pmic_fail = true;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_RAIL_ERROR);
    assert(!c.owned && !writes);
    reset(); memset(&c, 0, sizeof(c)); registers[0x137000 / 4] = 1;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_BAD_STATE);
    assert(!writes && !c.owned);
    reset(); memset(&c, 0, sizeof(c)); registers[0] = 0x12e000b1; /* Mariko is not this path */
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_BAD_STATE);
    assert(!writes && !c.owned);
    reset(); memset(&c, 0, sizeof(c)); target_khz = TG_GPU_OVERCLOCK_KHZ;
    assert(tg_gpu_clock_start(&io, &p, target_khz, &c) == TG_GPU_CLOCK_OK);
    assert(c.target_khz == 768000 && c.measured_khz == 768000);
    assert(c.rail_uv == 1100000 && (vout & 0x7f) == 79);
    assert(tg_gpu_clock_stop(&io, &c) == TG_GPU_CLOCK_OK);
    assert(tg_gpu_clock_start(&io, &p, 1234567, &c) == TG_GPU_CLOCK_BAD_STATE);
    puts("T210 legacy GPCPLL ordering, counter and unwind tests passed (host model).");
    return 0;
}
