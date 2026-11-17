        // ---- Erista (T210) GPU render/bring-up node: ACPI\SWOA0F02 ------------
        // Self-contained: the constants mirror
        // drivers/t210-icosa/drivers/TegraGpu/platform/t210_resources.h and are
        // checked against it by tools/check_acpi_resources.py.
        // Publishing resources does not power up the GPU. The driver sequences
        // MAX77620 GPIO6 -> MAX77621 itself. I2C connection ORDER IS ABI:
        //   [0] 0x1C MAX77621 GPU rail (Exclusive)
        //   [1] 0x3C MAX77620 (Shared; PWRB also holds it, see PWRB patch)
#ifdef SWITCH_ERISTA_GPU
        Device (GPU0)
        {
            Name (_HID, "SWOA0F02")
            Name (_UID, Zero)
            Name (_CLS, Package () { 0x03, 0x02, Zero })
            Name (_DEP, Package () { \_SB.I2C5 })
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x57000000, 0x01000000)   // GPU BAR0
                Memory32Fixed (ReadWrite, 0x58000000, 0x01000000)   // GPU BAR1
                Memory32Fixed (ReadWrite, 0x538F0000, 0x00001000)   // GPU aux
                Memory32Fixed (ReadWrite, 0x60006280, 0x00000018)   // CAR clk/rst X
                Memory32Fixed (ReadWrite, 0x6000667C, 0x00000004)   // PLLP_OUT5
                Memory32Fixed (ReadWrite, 0x7000E6D4, 0x00000004)   // PMC GPU clamp
                Memory32Fixed (ReadWrite, 0x70019970, 0x00000008)   // MC flush
                Memory32Fixed (ReadWrite, 0x70019AA8, 0x00000008)   // MC ASID
                Memory32Fixed (ReadOnly,  0x70000804, 0x00000004)   // APB_MISC HIDREV
                Memory32Fixed (ReadOnly,  0x7000F930, 0x00000004)   // FUSE speedo
                Memory32Fixed (ReadOnly,  0x7000FB04, 0x00000004)   // FUSE calib
#ifdef SWITCH_ERISTA_GPU_IRQ
                Memory32Fixed (ReadWrite, 0x60004414, 0x0000001C)   // LIC (SPI 158 fwd)
#endif
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 189 } // SPI 157 stall
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 190 } // SPI 158 nonstall
                I2cSerialBusV2 (0x001C, ControllerInitiated, 400000, AddressingMode7Bit, "\\_SB.I2C5", 0x00, ResourceConsumer, , Exclusive, )
                I2cSerialBusV2 (0x003C, ControllerInitiated, 400000, AddressingMode7Bit, "\\_SB.I2C5", 0x00, ResourceConsumer, , Shared, )
            })
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }
#endif
