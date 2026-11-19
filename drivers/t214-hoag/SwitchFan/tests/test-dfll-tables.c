/* SPDX-License-Identifier: GPL-2.0-only
 * Captured Noble/Switch Lite Speedo=1630, process=0, AggressiveSLT fixtures.
 * These arithmetic checks do not execute the CPU DFLL or validate voltages.
 */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/dfll_tables.h"
#include "../src/t214/nvpmodel_profiles.h"

int main(void)
{
    struct sw_dfll_table t;
    struct sw_dfll_request r;
    static const uint32_t mv[SW_DFLL_OPP_COUNT] = {
        600,600,600,600,600,600,600,620,645,675,705,740,775,835,
        875,920,965,1000,1050,1070,1070,1110,1165
    };
    static const uint8_t lut[SW_DFLL_LUT_SIZE] = {
        0x46,0x49,0x4a,0x4d,0x4f,0x52,0x55,0x58,0x5b,0x5e,0x62,
        0x65,0x69,0x6c,0x70,0x74,0x75,0x79,0x7d,0x81,0x85,0x86,
        0x8a,0x8e,0x8f,0x94,0x96,0x9b,0xa0,0xa4,0xaa,0xac,0xb7
    };
    assert(!SwDfllBuildTable(0, 0, 1600000, &t));
    assert(SwDfllBuildTable(1630, 0, 1600000, &t));
    assert(t.lut_size == SW_DFLL_LUT_SIZE && !memcmp(t.lut, lut, sizeof(lut)));
    assert(t.out_rate_min_khz == 816000 && t.tune0_low == 0xff90);
    for (unsigned i = 0; i < SW_DFLL_OPP_COUNT; ++i) assert(t.opp[i].uv == mv[i]*1000u);
    assert(SwDfllCodeUv(t.lut[SwDfllLutCeil(&t, 800000)]) == 810000);
    assert(SwDfllCodeUv(t.lut[SwDfllLutCeil(&t, 850000)]) == 855000);
    assert(SwDfllCodeUv(t.lut[SwDfllLutFloor(&t, 1060000)]) == 1050000);
    assert(SwDfllCodeUv(t.lut[SwDfllLutFloor(&t, 1010000)]) == 1000000);
    assert(SwDfllRateRequest(&t, 1963500, 1785000, &r));
    assert(r.mult == 77 && r.scale == 255 && r.scaled_khz == 1963500);
    assert(SwDfllRateRequest(&t, 2397000, 1785000, &r));
    assert(r.mult == 94 && r.scaled_khz == 2397000);
    assert(SwDfllRateRequest(&t, 102000, 816000, &r));
    assert(r.mult == 32 && r.scale == 31 && r.scaled_khz == 102000);
    assert(!SwDfllRateRequest(&t, 3000000, 1785000, &r));
    assert(sw_nvpmodel_profiles[SwNvpHandheld].cpu_max_khz == 1581000);
    assert(sw_nvpmodel_profiles[SwNvpHandheld].gpu_max_khz == 537600);
    assert(sw_nvpmodel_profiles[SwNvpConsole].gpu_min_khz == 0);
    assert(sw_nvpmodel_profiles[SwNvpOcAll].cpu_max_khz == 3000000);
    assert(sw_nvpmodel_profiles[SwNvpOcAll].gpu_max_khz == 2000000);
    assert(sw_nvpmodel_profiles[SwNvpPerfAll].cpu_min_khz == 1224000);
    assert(sw_nvpmodel_profiles[SwNvpPerfOcAll].gpu_min_khz == 460800);
    assert(SwDfllBuildTable(1630, 0, 2666000, &t) && t.opp[0].uv == 600000);
    assert(SwDfllBuildTable(1630, 0, 2666001, &t) && t.opp[0].uv == 620000);
    assert(SwDfllBuildTable(1630, 1, 3000000, &t) && t.opp[0].uv == 600000 && t.tune0_low == 0xffa0);
    puts("PASS: Noble CPU OPPs, all 33 DFLL LUT entries, thermal rounding, rate requests and seven nvpmodel definitions (arithmetic only)");
    return 0;
}
