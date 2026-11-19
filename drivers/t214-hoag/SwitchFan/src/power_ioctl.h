/* SPDX-License-Identifier: MIT OR GPL-2.0-only */
#ifndef SWITCHFAN_POWER_IOCTL_H
#define SWITCHFAN_POWER_IOCTL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <stdint.h>
#endif

/* Public \\.\SwitchBacklight power mode & live hardware readback IOCTLs.
 * FILE_DEVICE_UNKNOWN (0x22), METHOD_BUFFERED (0):
 *   0x812 READ  -> (0x22 << 16) | (1 << 14) | (0x812 << 2) = 0x00226048
 *   0x813 WRITE -> (0x22 << 16) | (2 << 14) | (0x813 << 2) = 0x0022A04C
 */
#define IOCTL_SWFAN_GET_POWER_STATUS 0x00226048u
#define IOCTL_SWFAN_SET_POWER_MODE   0x0022A04Cu

#define SWFAN_POWER_STATUS_VERSION 2u

enum SwFanPowerMode {
    SwFanPowerModeConsole         = 0,
    SwFanPowerModeHandheld        = 1,
    SwFanPowerModeOcCpu           = 2,
    SwFanPowerModeOcGpu           = 3,
    SwFanPowerModeOcAll           = 4,
    SwFanPowerModePerfAll         = 5,
    SwFanPowerModePerfOcAll       = 6,
    SwFanPowerModeDefaultFixed    = 7, /* internal failure recovery; normal baseline is Handheld */
    SwFanPowerModeCount           = 8
};

/* Reject old four-byte setters: their Mode 0 meant a different policy. */
typedef struct { unsigned int Version, Mode; } SWFAN_POWER_REQUEST;
#define IOCTL_SWFAN_CPU_LOAD 0x0022A050u
typedef struct { unsigned int Version, BusyPermille; } SWFAN_CPU_LOAD;

#define SWFAN_POWER_FLAG_CPU_APPLIED     0x01u
#define SWFAN_POWER_FLAG_CPU_THROTTLED   0x02u
#define SWFAN_POWER_FLAG_GPU_BRIDGE_SEEN 0x04u
#define SWFAN_POWER_FLAG_SCREEN_OFF      0x08u
#define SWFAN_POWER_FLAG_LIVE_GPU_COUNTER 0x10u

typedef struct _SWFAN_POWER_STATUS {
    unsigned int Version;          /* SWFAN_POWER_STATUS_VERSION (2) */
    unsigned int IsLite;           /* 1 on T210B01 Switch Lite */
    unsigned int CpuConfigured;    /* 1 when MAX77812 M4 + PLLX handoff is active */
    unsigned int PowerMode;        /* Switchroot 0..6, Windows recovery baseline 7 */
    unsigned int CpuTargetKhz;     /* target CPU kHz for current mode */
    unsigned int CpuActualKhz;     /* live CAR PLLX or DFLL monitor readback in kHz */
    unsigned int CpuBootKhz;       /* UEFI boot PLLX safe step (~998400 kHz) */
    unsigned int CpuMv;            /* live MAX77812 M4 readback in mV */
    unsigned int RamActualKhz;     /* UEFI EMC rate in kHz; zero means unavailable */
    unsigned int GpuPolicyMode;    /* 0 = Fixed, 1 = HighOPT NAPLL DVFS */
    unsigned int GpuPolicyMinKhz;  /* policy floor in kHz */
    unsigned int GpuPolicyMaxKhz;  /* policy ceiling in kHz */
    unsigned int GpuReportedKhz;   /* live GPCCLK counter kHz reported by KMD */
    unsigned int GpuReportedUv;    /* live GPU M1 uV reported by KMD / PMIC */
    unsigned int GpuStatusWord;    /* TG_GPU_DVFS_STATUS_PACK from KMD */
    unsigned int TempC;            /* max(TMP451 remote, local) in C */
    unsigned int TempValid;        /* 1 when TMP451 readback succeeded */
    unsigned int FanDuty;          /* current PWM duty 0..236 */
    unsigned int Flags;            /* SWFAN_POWER_FLAG_* */
    /* Keep the original 19-DWORD read-only prefix stable. */
    unsigned int CpuPolicyMinKhz, CpuPolicyMaxKhz, CpuRequestedMaxKhz;
    unsigned int GpuRequestedMaxKhz, CpuSpeedo, CpuProcessId;
    unsigned int CpuDfllAvailable, CpuDfllActive, CpuLastError, CpuLoadPermille;
    unsigned int GpuSampleAgeMs; /* ~0u means no valid hardware counter sample */
    unsigned int SocTempC, BoardTempC, TempSampleAgeMs; /* TMP451 remote/local; not two GPU/CPU sensors */
} SWFAN_POWER_STATUS;

#endif
