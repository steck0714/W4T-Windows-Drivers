/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SWITCHFAN_T214_GPU_PMIC_H
#define SWITCHFAN_T214_GPU_PMIC_H
#include "../power_ioctl.h"
/* GPU transport and private ABI are maintained only in mesa-switch-woa. */
#include "integration/switch-lite/windows/SwitchFan/src/t214/gpu_pmic.h"

static VOID SwFanPublishGpuPolicy(PSWFAN_DEVICE Dev, ULONG Mode, ULONG Minimum, ULONG Maximum)
{
    ULONG Seq = (Dev->PolicySeq + 1u) & 0xfu;
    if (!Seq) Seq = 1u;
    Dev->PolicySeq = Seq;
    (VOID)InterlockedExchange(&Dev->GpuDvfsPolicy,
        (LONG)TG_GPU_DVFS_POLICY_PACK(Mode, Minimum, Maximum, Seq));
}

/* Do not retry an unsuccessful DFLL transition from the periodic timer.
 * Only an explicit baseline selection clears this failure latch. */
static NTSTATUS SwFanDvfsFailureLocked(PSWFAN_DEVICE Dev, NTSTATUS Cause)
{
    NTSTATUS Recovery = SwFanDfllStopLocked(Dev);
    Dev->Dfll.FaultLatched = TRUE;
    Dev->Dfll.LastStatus = NT_SUCCESS(Recovery) ? Cause : Recovery;
    Dev->CpuThrottled = TRUE;
    Dev->PowerMode = SwFanPowerModeDefaultFixed;
    SwFanPublishGpuPolicy(Dev, 0, 460800u, 460800u);
    SwFanTrace(L"CpuLastError", (ULONG)Dev->Dfll.LastStatus);
    return Dev->Dfll.LastStatus;
}

static NTSTATUS SwFanDvfsUpdateLocked(PSWFAN_DEVICE Dev, ULONG Temp, BOOLEAN Valid)
{
    NTSTATUS Status;
    Dev->LastTemp = Temp;
    Dev->TempValid = Valid;
    if (Dev->Dfll.FaultLatched) return Dev->Dfll.LastStatus;
    if (!Valid || Temp >= 96u)
        return SwFanDvfsFailureLocked(Dev, STATUS_DEVICE_NOT_READY);
    Status = SwFanDfllApplyProfileLocked(Dev, Dev->PowerMode);
    return NT_SUCCESS(Status) ? Status : SwFanDvfsFailureLocked(Dev, Status);
}

static NTSTATUS SwFanSetPowerMode(PSWFAN_DEVICE Dev, ULONG Mode)
{
    BOOLEAN ScreenOff = FALSE;
    ULONG GpuMode = 0, GpuMinKhz = 460800u, GpuMaxKhz = 460800u;
    NTSTATUS Status = STATUS_SUCCESS;
    if (Mode >= SwFanPowerModeCount) return STATUS_INVALID_PARAMETER;
    if (!Dev->IsLite) return STATUS_NOT_SUPPORTED;
    if (Mode != SwFanPowerModeDefaultFixed && !SwFanGpuLiveCounter(Dev)) return STATUS_NOT_SUPPORTED;
    SwFanCpuLock(Dev);
    if (Dev->Stopping || !Dev->CpuConfigured || !Dev->Pmic || !Dev->Car) {
        SwFanCpuUnlock(Dev); return STATUS_DEVICE_NOT_READY;
    }
    if (Dev->BacklightLock) WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    ScreenOff = Dev->BacklightReady && Dev->DisplayState == 0;
    if (Dev->BacklightLock) WdfWaitLockRelease(Dev->BacklightLock);
    if (Mode == SwFanPowerModeDefaultFixed) {
        Status = SwFanDfllStopLocked(Dev);
        if (!NT_SUCCESS(Status)) { SwFanCpuUnlock(Dev); return Status; }
        Dev->Dfll.FaultLatched = FALSE;
        Dev->CpuThrottled = !Dev->TempValid || Dev->LastTemp >= 80u;
        Status = SwCpuApplyLocked(Dev, Dev->BaseCpuOverclockEnabled &&
                                  !ScreenOff && Dev->TempValid && !Dev->CpuThrottled);
    } else {
        const struct sw_nvpmodel_profile *profile = &sw_nvpmodel_profiles[Mode];
        if (ScreenOff) Status = STATUS_DEVICE_NOT_READY;
        else Status = SwFanDfllApplyProfileLocked(Dev, Mode);
        GpuMode = 1;
        GpuMinKhz = profile->gpu_min_khz ? profile->gpu_min_khz : 76800u;
        GpuMaxKhz = profile->gpu_max_khz > 1228800u ? 1228800u : profile->gpu_max_khz;
    }
    if (!NT_SUCCESS(Status)) {
        if (Dev->Dfll.Control.prepared || Dev->Dfll.ClocksOwned ||
            Status == STATUS_DEVICE_HARDWARE_ERROR) Status = SwFanDvfsFailureLocked(Dev, Status);
        SwFanCpuUnlock(Dev); return Status;
    }
    Dev->CpuOverclockEnabled = Dev->BaseCpuOverclockEnabled;
    SwFanPublishGpuPolicy(Dev, GpuMode, GpuMinKhz, GpuMaxKhz);
    Dev->PowerMode = Mode;
    SwFanCpuUnlock(Dev);
    SwFanTrace(L"PowerMode", Mode);
    return STATUS_SUCCESS;
}

static VOID SwFanGetPowerStatus(PSWFAN_DEVICE Dev, SWFAN_POWER_STATUS *Out)
{
    ULONG Policy;
    BOOLEAN ScreenOff = FALSE;
    UCHAR Code = 0;

    SwFanCpuLock(Dev);
    Policy = (ULONG)InterlockedCompareExchange(&Dev->GpuDvfsPolicy, 0, 0);
    RtlZeroMemory(Out, sizeof(*Out));
    Out->Version = SWFAN_POWER_STATUS_VERSION;
    Out->IsLite = Dev->IsLite ? 1u : 0u;
    Out->CpuConfigured = Dev->CpuConfigured ? 1u : 0u;
    Out->PowerMode = Dev->PowerMode;
    Out->CpuBootKhz = Dev->BootCpuKhz;
    Out->CpuTargetKhz = (Dev->IsLite && Dev->CpuConfigured) ?
        (Dev->CpuOverclockEnabled ? CPU_TARGET_KHZ : Dev->BootCpuKhz) : Dev->Config.CpuKhz;
    if (Dev->PowerMode < SwNvpCount) Out->CpuTargetKhz = Dev->Dfll.Control.requested_khz;
    Out->CpuActualKhz = Dev->Car != NULL ? SwCpuKhz(Dev) : 0u;
    /* A boot handoff is not a runtime sample. Consumers that need the boot
     * value, sample age or enumeration error use GET_EMC_STATUS separately. */
    Out->RamActualKhz = !Dev->Stopping && Dev->Emc.SampleTime &&
        KeQueryInterruptTime() - Dev->Emc.SampleTime <= 60000000ULL ?
        Dev->Emc.Published.ActualKhz : 0u;
    Out->GpuPolicyMode = TG_GPU_DVFS_POLICY_MODE(Policy);
    Out->GpuPolicyMinKhz = TG_GPU_DVFS_POLICY_MIN_KHZ(Policy);
    Out->GpuPolicyMaxKhz = TG_GPU_DVFS_POLICY_MAX_KHZ(Policy);
    Out->GpuReportedKhz = Dev->GpuReportedKhz;
    Out->GpuReportedUv = Dev->GpuReportedUv;
    Out->GpuStatusWord = Dev->GpuStatusWord;
    Out->TempC = Dev->LastTemp;
    Out->TempValid = Dev->TempValid ? 1u : 0u;
    Out->FanDuty = Dev->Duty;

    if (Dev->IsLite && !Dev->Stopping && Dev->Pmic != NULL) {
        SwFanPmicLock(Dev);
        if (NT_SUCCESS(SwFanReadReg(Dev->Pmic, CPU_M4_VOUT, &Code)))
            Out->CpuMv = 250u + (ULONG)Code * 5u;
        if (NT_SUCCESS(SwFanReadReg(Dev->Pmic, GPU_M1_VOUT_REG, &Code)))
            Out->GpuReportedUv = (250u + (ULONG)Code * 5u) * 1000u;
        SwFanPmicUnlock(Dev);
    }

    if (Dev->BacklightLock != NULL) WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    ScreenOff = Dev->IsLite && Dev->BacklightReady && Dev->DisplayState == 0;
    if (Dev->BacklightLock != NULL) WdfWaitLockRelease(Dev->BacklightLock);

    if (Dev->CpuApplied)    Out->Flags |= SWFAN_POWER_FLAG_CPU_APPLIED;
    if (Dev->CpuThrottled)  Out->Flags |= SWFAN_POWER_FLAG_CPU_THROTTLED;
    if (Dev->GpuBridgeSeen) Out->Flags |= SWFAN_POWER_FLAG_GPU_BRIDGE_SEEN;
    if (SwFanGpuLiveCounter(Dev)) Out->Flags |= SWFAN_POWER_FLAG_LIVE_GPU_COUNTER;
    if (ScreenOff)          Out->Flags |= SWFAN_POWER_FLAG_SCREEN_OFF;
    Out->CpuPolicyMinKhz = Dev->Dfll.CpuMinKhz;
    Out->CpuPolicyMaxKhz = Dev->Dfll.CpuMaxKhz;
    Out->CpuRequestedMaxKhz = Dev->Dfll.CpuRequestedMaxKhz;
    Out->GpuRequestedMaxKhz = Dev->PowerMode < SwNvpCount ?
        sw_nvpmodel_profiles[Dev->PowerMode].gpu_max_khz : 460800u;
    Out->CpuSpeedo = Dev->Dfll.Handoff.CpuSpeedo;
    Out->CpuProcessId = Dev->Dfll.Handoff.CpuProcessId;
    Out->CpuDfllAvailable = SwFanDfllAvailable(Dev);
    Out->CpuDfllActive = Dev->Dfll.Control.active;
    Out->CpuLastError = (ULONG)Dev->Dfll.LastStatus;
    Out->CpuLoadPermille = Dev->Dfll.LoadPermille;
    Out->GpuSampleAgeMs = Dev->GpuSampleTime ?
        (ULONG)((KeQueryInterruptTime() - Dev->GpuSampleTime) / 10000u) : ~0u;
    Out->SocTempC = Dev->SocTempC;
    Out->BoardTempC = Dev->BoardTempC;
    Out->TempSampleAgeMs = Dev->TempSampleTime && Dev->TempValid ?
        (ULONG)((KeQueryInterruptTime() - Dev->TempSampleTime) / 10000u) : ~0u;
    SwFanCpuUnlock(Dev);
}

#endif
