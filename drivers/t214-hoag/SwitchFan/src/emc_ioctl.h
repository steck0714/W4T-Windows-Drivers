/* SPDX-License-Identifier: MIT */
#ifndef SWITCHFAN_EMC_IOCTL_H
#define SWITCHFAN_EMC_IOCTL_H
/* Read-only runtime status; the old v2 power ABI stays the same size. */
#define IOCTL_SWFAN_GET_EMC_STATUS 0x00226054u
#define SWFAN_EMC_STATUS_VERSION 1u
#define SWFAN_EMC_CLOCK_IDENTIFIED 1u
#define SWFAN_EMC_RATE_VALID 2u
#define SWFAN_EMC_RATES_ENUMERATED 4u
#define SWFAN_EMC_DYNAMIC_ENABLED 8u
#define SWFAN_EMC_MAILBOX_FAULT 16u
#define SWFAN_EMC_QOS_READY 32u
#define SWFAN_EMC_ACTMON_VALID 64u
#define SWFAN_EMC_TRANSITION_FAULT 128u
#define SWFAN_EMC_WATERMARK_IRQ 256u /* at least one real MC_ALL interrupt handled */
typedef struct {
    unsigned int Version, Flags, ActualKhz, BootKhz, SampleAgeMs;
    int LastError, EnumerationError;
    unsigned int RateCount, RatesKhz[32];
    int ActmonError;
    unsigned int AverageActiveKhz, RawActiveKhz, TargetKhz, DisplayFloorKhz;
    int QosError;
    unsigned int TransitionCount;
} SWFAN_EMC_STATUS;
#endif
