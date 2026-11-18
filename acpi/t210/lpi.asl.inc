        // ---- CPU idle states (_LPI) for Erista (T210), opt-in: SWITCH_LPI -------
        // Included inside each Device(CPUn). PSCI power_state values are the
        // extended-state-id format Switchroot's bl31 uses (PSCI_EXTENDED_STATE_ID=1,
        // type bit 30 = power down), from plat/nvidia/tegra/include/t210/tegra_def.h:
        //   PSTATE_ID_CORE_POWERDN = 7  -> 0x40000007
        // Evidence for the defaults (see docs/SLEEP-DESIGN-ja.md):
        //  * Switchroot's NX device tree sets idle-states/c7 status="disabled"
        //    (reference/l4t/platform-t210-nx/.../tegra210-odin-common.dtsi:1132),
        //    so core power-down idle is OFF on Linux for this device. It is therefore
        //    published here as DISABLED (Flags bit0). Flip SWITCH_LPI_C7_ENABLE to try it.
        //  * Cluster idle (id 16) and SoC power-down (id 27 = SC7) are deliberately NOT
        //    described: SC7 is a whole-system suspend that needs every device quiesced
        //    (PEP / Modern Standby), not a per-CPU idle state.
        // Package layout: ACPI 6.x 8.4.4.3. Latencies are conservative placeholders to
        // be replaced by measurements from a real unit.
#ifndef SWITCH_LPI_C7_FLAGS
#ifdef SWITCH_LPI_C7_ENABLE
#define SWITCH_LPI_C7_FLAGS 0x2
#else
#define SWITCH_LPI_C7_FLAGS 0x3
#endif
#endif
        Method (_LPI, 0, NotSerialized)
        {
            Return (Package ()
            {
                Zero,                 // Revision
                Zero,                 // LevelID
                2,                    // Count
                Package ()            // C1: WFI (PSCI standby, parameter 0)
                {
                    1, 1, 0x2, Zero, Zero, Zero,
                    ResourceTemplate () { Register (FFixedHW, 0x20, 0x00, 0x0000000000000000, 0x03) },
                    Zero, Zero,
                    "WFI"
                },
                Package ()            // C7: core power down (PSCI 0x40000007)
                {
                    2000, 500, SWITCH_LPI_C7_FLAGS, 0x1, Zero, Zero,
                    ResourceTemplate () { Register (FFixedHW, 0x20, 0x00, 0x0000000040000007, 0x03) },
                    Zero, Zero,
                    "CORE-PD"
                }
            })
        }
