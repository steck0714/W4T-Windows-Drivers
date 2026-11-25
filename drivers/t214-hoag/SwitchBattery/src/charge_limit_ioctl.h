/* SPDX-License-Identifier: MIT OR GPL-2.0-only */
#ifndef SWITCHBATTERY_CHARGE_LIMIT_IOCTL_H
#define SWITCHBATTERY_CHARGE_LIMIT_IOCTL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <stdint.h>
#endif

/* Public \\.\SwitchBattery SoC charging limit & live hardware readback IOCTLs.
 * FILE_DEVICE_UNKNOWN (0x22), METHOD_BUFFERED (0):
 *   0x814 READ  -> (0x22 << 16) | (1 << 14) | (0x814 << 2) = 0x00226050
 *   0x815 WRITE -> (0x22 << 16) | (2 << 14) | (0x815 << 2) = 0x0022A054
 *
 * Semantics match Switch L4T Noble bq2419x_soc_regulation_control center +-5%
 * SoC hysteresis (55 -> 50-60% band, 85 -> 80-90% band, 0 -> Off / 100%),
 * EXCEPT Windows intentionally keeps external VBUS power connected (BQ24193
 * REG00 EN_HIZ = 0) above BandMax instead of disconnecting external input to
 * actively drain the battery. Only REG01 CHG_CONFIG (0x10 vs 0x00) is toggled
 * while preserving OTG VBUS, USB-C PD IINLIM, JEITA, and 8h safety timer.
 */
#define IOCTL_SWBAT_GET_CHARGE_LIMIT 0x00226050u
#define IOCTL_SWBAT_SET_CHARGE_LIMIT 0x0022A054u

#define SWBAT_CHARGE_LIMIT_STATUS_VERSION 1u

#define SWBAT_CHARGE_LIMIT_OFF        0u
#define SWBAT_CHARGE_LIMIT_DOCKED     55u
#define SWBAT_CHARGE_LIMIT_HIGH       85u
#define SWBAT_CHARGE_LIMIT_HYSTERESIS 5u

#define SWBAT_CHARGE_FLAG_CHARGER_READY   0x01u
#define SWBAT_CHARGE_FLAG_INHIBITED       0x02u
#define SWBAT_CHARGE_FLAG_EXTERNAL_ONLINE 0x04u
#define SWBAT_CHARGE_FLAG_CHARGING        0x08u
#define SWBAT_CHARGE_FLAG_OTG_ACTIVE      0x10u
#define SWBAT_CHARGE_FLAG_HIZ_ACTIVE      0x20u
#define SWBAT_CHARGE_FLAG_SUSPEND_HOLD    0x40u

typedef struct _SWBAT_CHARGE_LIMIT_STATUS {
    unsigned int Version;          /* SWBAT_CHARGE_LIMIT_STATUS_VERSION (1) */
    unsigned int LimitCenter;      /* 0 (Off), 55 (50-60%), or 85 (80-90%) */
    unsigned int BandMin;          /* 0 when Off, else LimitCenter - 5 */
    unsigned int BandMax;          /* 100 when Off, else LimitCenter + 5 */
    unsigned int CapacityPercent;  /* live MAX17050 RepSOC 0..100 */
    unsigned int VoltageMv;        /* live MAX17050 VCELL in mV */
    unsigned int Reg00InputSrc;    /* live BQ24193 REG00 readback */
    unsigned int Reg01PwrOn;       /* live BQ24193 REG01 readback */
    unsigned int Reg08SysStat;     /* live BQ24193 REG08 readback */
    unsigned int LastApplyStatus;  /* NTSTATUS of last BQ24193 apply/readback */
    unsigned int Flags;            /* SWBAT_CHARGE_FLAG_* */
} SWBAT_CHARGE_LIMIT_STATUS;

#endif
