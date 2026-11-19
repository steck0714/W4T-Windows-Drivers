/* SPDX-License-Identifier: MIT OR GPL-2.0-only
 * Numeric policy facts from the captured Switchroot Noble T210B01 nvpmodel.
 * CPU/GPU caps are requests; the clock driver must resolve supported OPPs.
 * Source capture SHA256: 87f3b515db3aed79623142f25bda1eedfd5c168288d7bf944b928fc27c415224
 */
#ifndef SWITCHFAN_NVP_MODEL_PROFILES_H
#define SWITCHFAN_NVP_MODEL_PROFILES_H
#include <stdint.h>

enum sw_nvpmodel_id {
    SwNvpConsole = 0, SwNvpHandheld = 1, SwNvpOcCpu = 2, SwNvpOcGpu = 3,
    SwNvpOcAll = 4, SwNvpPerfAll = 5, SwNvpPerfOcAll = 6, SwNvpCount = 7,
};
struct sw_nvpmodel_profile {
    const char *name;
    uint32_t cpu_min_khz, cpu_max_khz;
    uint32_t gpu_min_khz, gpu_max_khz;
    uint32_t cpu_overclock;
};
static const struct sw_nvpmodel_profile sw_nvpmodel_profiles[SwNvpCount] = {
    { "Console",        0, 1963000,      0,  844800, 0 },
    { "Handheld",       0, 1581000,      0,  537600, 0 },
    { "OC_CPU",         0, 3000000,      0,  844800, 1 },
    { "OC_GPU",         0, 1963000,      0, 2000000, 0 },
    { "OC_All",         0, 3000000,      0, 2000000, 1 },
    { "Perf_All", 1224000, 1963000, 460800,  844800, 0 },
    { "Perf_OC_All",1224000,3000000,460800, 2000000, 1 },
};
#endif
