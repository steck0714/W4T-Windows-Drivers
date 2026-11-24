/* SPDX-License-Identifier: MIT OR GPL-2.0-only */
#ifndef SWITCHBATTERY_TELEMETRY_IOCTL_H
#define SWITCHBATTERY_TELEMETRY_IOCTL_H
#define IOCTL_SWBAT_GET_TELEMETRY 0x00226060u
#define SWBAT_TELEMETRY_VERSION 1u
#define SWBAT_TELEMETRY_VALID 1u
#define SWBAT_TELEMETRY_MAX_AGE_MS 6000u
/* Battery-terminal net power, NOT charger input / CPU / GPU power.
 * Positive = into the battery, negative = discharging. Cached polling only. */
typedef struct {
    unsigned int Version, Flags, VoltageUv;
    int CurrentUa, PowerUw;
    unsigned int SampleAgeMs, LastStatus, RawCurrent, Cgain, GaugeId;
} SWBAT_TELEMETRY_STATUS;
#endif
