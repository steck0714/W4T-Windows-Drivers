#include <assert.h>
#include <stdio.h>
#include "psci_t210.h"
int main(void) {
    assert(t210_psci_power_state(T210_PSTATE_CORE_POWERDN) == 0x40000007u);
    assert(t210_psci_power_state(T210_PSTATE_CLUSTER_IDLE) == 0x40000010u);
    assert(t210_psci_power_state(T210_PSTATE_SOC_POWERDN)  == 0x4000001Bu);
    assert(t210_psci_state_supported(0x40000007u) && t210_psci_state_supported(0x4000001Bu));
    assert(!t210_psci_state_supported(0x40000008u));        /* unknown id */
    assert(!t210_psci_state_supported(0x80000007u));        /* reserved bit set */
    assert(!t210_psci_state_supported(0));                  /* standby id 0 is not a Tegra state id */
    t210_sc7_pre p = {0, 1, 0x80000000u};
    assert(t210_sc7_precheck(&p) == PSCI_RET_NOT_SUPPORTED); /* no sc7entry-fw */
    p.bl31_has_sc7entry_fw = 1; p.other_cpus_off = 0;
    assert(t210_sc7_precheck(&p) == PSCI_RET_DENIED);
    p.other_cpus_off = 1; p.resume_entry_pa = 0;
    assert(t210_sc7_precheck(&p) == PSCI_RET_INVALID_PARAMS);
    p.resume_entry_pa = 0x80000002u;
    assert(t210_sc7_precheck(&p) == PSCI_RET_INVALID_PARAMS); /* unaligned */
    p.resume_entry_pa = 0x80000000u;
    assert(t210_sc7_precheck(&p) == PSCI_RET_SUCCESS);
    puts("psci_t210 ok"); return 0;
}
