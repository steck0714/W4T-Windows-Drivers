/* SPDX-License-Identifier: MIT */
#ifndef SWITCH_CORE_VOLTAGE_IOCTL_H
#define SWITCH_CORE_VOLTAGE_IOCTL_H
/* Kernel-only contract to the existing MAX77620 owner. No second SPB open. */
#define IOCTL_SWCORE_EMC_VOTE 0x00222414u
#define SWCORE_VOTE_VERSION 1u
#define SWCORE_DEVICE_NAME L"\\Device\\SwitchCoreVoltage"
typedef struct {
    unsigned int Version, SetVote, MinimumUv;
    unsigned int AppliedUv, BootFloorUv, VoteUv;
} SWCORE_VOTE;
#endif
