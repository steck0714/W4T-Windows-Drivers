/* SPDX-License-Identifier: GPL-2.0-only
 * PSCI helpers for Tegra210 (Erista) as implemented by Switchroot's bl31.
 * Constants mirror reference/atf/tegra_def.h (switch-l4t-atf ed2ef6be) and
 * reference/atf/plat_psci_handlers.c. Pure logic: no WDK dependency, host-testable.
 * Nothing here talks to hardware; a PEP / HAL shim must issue the SMC itself. */
#ifndef PSCI_T210_H
#define PSCI_T210_H
#include <stdint.h>

/* PSCI 1.x SMC function IDs (Arm DEN0022, SMC64 where the call takes 64-bit args). */
#define PSCI_FN_VERSION          0x84000000u
#define PSCI_FN_CPU_SUSPEND_64   0xC4000001u
#define PSCI_FN_CPU_OFF          0x84000002u
#define PSCI_FN_CPU_ON_64        0xC4000003u
#define PSCI_FN_AFFINITY_INFO_64 0xC4000004u
#define PSCI_FN_SYSTEM_OFF       0x84000008u
#define PSCI_FN_SYSTEM_RESET     0x84000009u
#define PSCI_FN_FEATURES         0x8400000Au
#define PSCI_FN_SYSTEM_SUSPEND_64 0xC400000Eu

/* PSCI return codes */
#define PSCI_RET_SUCCESS        0
#define PSCI_RET_NOT_SUPPORTED  (-1)
#define PSCI_RET_INVALID_PARAMS (-2)
#define PSCI_RET_DENIED         (-3)

/* Tegra210 state ids (tegra_def.h) */
#define T210_PSTATE_CORE_POWERDN 7u    /* affinity level 0 */
#define T210_PSTATE_CLUSTER_IDLE 16u   /* level 1 (CPU in core power down) */
#define T210_PSTATE_SOC_POWERDN  27u   /* level 2 = SC7, same as system suspend */

/* PSCI_EXTENDED_STATE_ID=1 (plat/nvidia/tegra/platform.mk): id in [27:0], type in bit 30. */
#define PSCI_PS_TYPE_POWERDOWN (1u << 30)
#define PSCI_PS_ID_MASK        0x0fffffffu
#define PSCI_PS_VALID_MASK     0xB0000000u  /* bits that must be zero */

static inline uint32_t t210_psci_power_state(uint32_t id) {
    return PSCI_PS_TYPE_POWERDOWN | (id & PSCI_PS_ID_MASK);
}
/* Mirrors tegra_soc_validate_power_state()'s switch: only these three ids exist. */
static inline int t210_psci_state_supported(uint32_t power_state) {
    uint32_t id;
    if (power_state & (PSCI_PS_VALID_MASK & ~PSCI_PS_TYPE_POWERDOWN)) return 0;
    id = power_state & PSCI_PS_ID_MASK;
    return id == T210_PSTATE_CORE_POWERDN || id == T210_PSTATE_CLUSTER_IDLE ||
           id == T210_PSTATE_SOC_POWERDN;
}
/* What the SoC needs before PSCI SYSTEM_SUSPEND can succeed on this bl31 (see
 * plat_psci_handlers.c: PSTATE_ID_SOC_POWERDN returns NOT_SUPPORTED when the bootloader
 * gave no sc7entry-fw and BPMP-FW is absent). hekate's L4T boot loads sc7entry.bin and
 * sc7exit.bin from the SD card and passes them to bl31. */
typedef struct { int bl31_has_sc7entry_fw; int other_cpus_off; uint64_t resume_entry_pa; } t210_sc7_pre;
static inline int t210_sc7_precheck(const t210_sc7_pre *p) {
    if (!p->bl31_has_sc7entry_fw) return PSCI_RET_NOT_SUPPORTED;
    if (!p->other_cpus_off)       return PSCI_RET_DENIED;        /* PSCI: all other cores OFF */
    if (!p->resume_entry_pa || (p->resume_entry_pa & 3u)) return PSCI_RET_INVALID_PARAMS;
    return PSCI_RET_SUCCESS;
}
#endif
