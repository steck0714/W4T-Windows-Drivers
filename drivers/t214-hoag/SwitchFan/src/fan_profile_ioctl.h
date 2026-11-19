/* SPDX-License-Identifier: MIT OR GPL-2.0-only */
#ifndef SWITCHFAN_PROFILE_IOCTL_H
#define SWITCHFAN_PROFILE_IOCTL_H
/* Versioned, separate from the stable power-status ABI. */
#define IOCTL_SWFAN_GET_FAN_PROFILE 0x0022605Cu
#define IOCTL_SWFAN_SET_FAN_PROFILE 0x0022A05Cu
#define SWFAN_PROFILE_VERSION 1u
enum SwFanProfile { SwFanProfileConsole, SwFanProfileHandheld, SwFanProfileCool, SwFanProfileCount };
typedef struct { unsigned int Version, Mode; } SWFAN_PROFILE_REQUEST;
#define SWFAN_PROFILE_VALID 1u
#define SWFAN_PROFILE_FAILSAFE 2u
typedef struct {
    unsigned int Version, Mode, Duty, TargetPwm, Flags, SampleAgeMs;
    int FilteredTempMc;
} SWFAN_PROFILE_STATUS;
#endif
