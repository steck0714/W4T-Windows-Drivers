/* W4T-CREDIT-BEGIN
 * Origin : ruje0504-2/Windows-for-Switch-Drivers (drivers/SwitchFan, commit de636f5), Lite (t214/Hoag) line;
 *          shared implementations come from hayato41200/Windows-for-Switch-Drivers
 *          (see drivers/t214-hoag/README.md).
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : see CREDITS.md (per-source table).
 * Credit block added by W4T (2026-11-19); full table in CREDITS.md.
 * W4T-CREDIT-END */
/* Shared Nintendo Switch fan/backlight driver. T214/HDH-001 is detected
 * from its dedicated PWM1 resource. New Lite firmware can additionally
 * expose PWM0 for backlight control. Clock/sensor setup is model-specific.
 */
#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <spb.h>
#include "t214/performance.h"
#include "backlight.h"
#ifdef SWITCH_LITE_GPU
#include "power_ioctl.h"
#include "t214/dfll_windows_types.h"
#include "t214/emc_windows_types.h"
#include "integration/switch-lite/windows/SwitchFan/src/t214/emc_gpu_vote.h"
#endif

#define PWM_CSR_0        0x00
#define IOCTL_SWBL_GET   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_READ_ACCESS)    /* 0x00226040 */
#define IOCTL_SWBL_SET   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_WRITE_ACCESS)   /* 0x0022A044 */

/* GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed. */
static const GUID SwFanDisplayStateGuid =
    { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };

typedef struct _SWBL_STATUS {
    ULONG Version;          /* 2 */
    ULONG Brightness;       /* user level, 8..255 */
    ULONG Duty;             /* what CSR_0 holds now */
    ULONG DisplayState;     /* 0 off, 1 on, 2 dimmed */
} SWBL_STATUS;

#define PWM_CSR_1        0x00  /* ACPI maps only 0x7000A010..13 */
#define PWM_CSR_EN       (1u << 31)
#define PWM_DUTY_MAX     236u
#define PWM_ABSOLUTE_OFF (0x100u << 16)
#define TMP451_LOCAL     0x00
#define TMP451_REMOTE    0x01
#include "fan_profile.h"
#define POLL_MS          2000
#define FAILSAFE_DUTY    153u

typedef struct _SWFAN_DEVICE {
    volatile UCHAR *Pwm;
    ULONG PwmLength;
    ULONG FanOffset;
    BOOLEAN IsLite;
    volatile UCHAR *BacklightPwm;
    ULONG BacklightLength;  /* zero when this aliases the Erista PWM map */
    BOOLEAN BacklightReady;
    WDFWAITLOCK BacklightLock;
    PVOID DisplayCallback;
    ULONG Brightness, DisplayState;
    volatile UCHAR *Car;
    LITE_PERFORMANCE Config;
    LARGE_INTEGER PmicIds[2];
    ULONG ConnectionCount;
    WDFIOTARGET Pmic;
    WDFWAITLOCK PmicLock;
    WDFWAITLOCK CpuLock;
    ULONG BootPllx, BootCpuKhz;
    UCHAR BootVout;
    BOOLEAN CpuConfigured, CpuApplied, CpuThrottled, CpuOverclockEnabled;
    BOOLEAN BaseCpuOverclockEnabled, GpuBridgeSeen;
    ULONG PowerMode, PolicySeq;
    volatile LONG GpuDvfsPolicy;
    ULONG GpuReportedKhz, GpuReportedUv, GpuStatusWord;
#ifdef SWITCH_LITE_GPU
    SWFAN_DFLL Dfll;
    SWFAN_EMC Emc;
    struct tg_emc_gpu_vote EmcGpuVote;
    ULONGLONG EmcGpuAbiTime;
    ULONGLONG GpuSampleTime;
    ULONGLONG GpuAbiTime;
    ULONGLONG TempSampleTime;
    ULONG SocTempC, BoardTempC;
#endif
    LARGE_INTEGER SensorId;
    WDFIOTARGET Sensor;
    WDFWAITLOCK SensorLock;
    WDFTIMER Timer;
#ifdef SWITCH_LITE_GPU
    WDFTIMER EmcTimer;
    WDFWAITLOCK EmcLock;
    WDFDPC EmcDpc;
    WDFWORKITEM EmcWorkItem;
#endif
    KSPIN_LOCK TimerLock;
    volatile BOOLEAN Stopping, TimerStopping;
    ULONG Duty;
    WDFWAITLOCK FanLock;
    ULONG FanProfile, FanTargetPwm, FanProfileFlags;
    ULONGLONG FanSampleTime;
    struct sw_fan_filter FanFilter;
    ULONG Failures;
    ULONG LastTemp;
    BOOLEAN TempValid;
    LONG Runs;
} SWFAN_DEVICE, *PSWFAN_DEVICE;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SWFAN_DEVICE, SwFanGetDevice)

typedef struct _SWFAN_TIMER {
    WDFDEVICE Device;
} SWFAN_TIMER, *PSWFAN_TIMER;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SWFAN_TIMER, SwFanGetTimer)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SwFanEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SwFanEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SwFanEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY SwFanEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT SwFanEvtD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT SwFanEvtSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP SwFanEvtSelfManagedIoCleanup;
EVT_WDF_TIMER SwFanEvtTimer;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND SwFanEvtSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART SwFanEvtRestart;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SwFanEvtIoDeviceControl;
POWER_SETTING_CALLBACK SwFanDisplayStateCallback;

static WCHAR SwFanParamsPath[256];

static VOID SwFanTrace(PCWSTR Name, ULONG Value)
{
    if (SwFanParamsPath[0] != L'\0' && KeGetCurrentIrql() == PASSIVE_LEVEL) {
        (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath, Name, REG_DWORD, &Value, sizeof(Value));
    }
}

/* ------------------------------------------------------------------- I2C */

static NTSTATUS SwFanOpenTarget(WDFDEVICE Device, LARGE_INTEGER Id, WDFIOTARGET *Target)
{
    WCHAR PathBuffer[64];
    UNICODE_STRING Path;
    WDF_IO_TARGET_OPEN_PARAMS Open;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Path, PathBuffer, sizeof(PathBuffer));
    Status = RtlUnicodeStringPrintf(&Path, L"\\Device\\RESOURCE_HUB\\%016I64x", (ULONG64)Id.QuadPart);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = WdfIoTargetCreate(Device, WDF_NO_OBJECT_ATTRIBUTES, Target);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&Open, &Path, GENERIC_READ | GENERIC_WRITE);
    Status = WdfIoTargetOpen(*Target, &Open);
    if (!NT_SUCCESS(Status)) {
        WdfObjectDelete(*Target);
        *Target = NULL;
    }
    return Status;
}

static NTSTATUS SwFanReadReg(WDFIOTARGET Target, UCHAR Register, PUCHAR Value)
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) Sequence;
    PSPB_TRANSFER_LIST_ENTRY Entries = Sequence.List.Transfers;
    WDF_MEMORY_DESCRIPTOR Input;
    WDF_REQUEST_SEND_OPTIONS Options;
    ULONG_PTR Bytes = 0;
    NTSTATUS Status;

    if (Target == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    SPB_TRANSFER_LIST_INIT(&Sequence.List, 2);
    Entries[0] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionToDevice, 0, &Register, 1);
    Entries[1] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionFromDevice, 0, Value, 1);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&Input, &Sequence, sizeof(Sequence));
    WDF_REQUEST_SEND_OPTIONS_INIT(&Options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    Options.Timeout = WDF_REL_TIMEOUT_IN_MS(500);
    Status = WdfIoTargetSendIoctlSynchronously(Target, NULL, IOCTL_SPB_EXECUTE_SEQUENCE, &Input, NULL, &Options, &Bytes);
    if (NT_SUCCESS(Status) && Bytes != 2) {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    return Status;
}

/* Same TMP451 configuration as Hekate: normal range, Mariko -12.5 C
 * remote offset, 4-second conversion period. All transfers run at PASSIVE_LEVEL. */
static NTSTATUS SwFanConfigureSensor(WDFIOTARGET Target)
{
    static const UCHAR Setup[][2] = {{0x09, 0x80}, {0x11, 0xF3}, {0x12, 0x80}, {0x0A, 0x02}};
    ULONG Index;
    for (Index = 0; Index < RTL_NUMBER_OF(Setup); ++Index) {
        UCHAR Buffer[2] = {Setup[Index][0], Setup[Index][1]};
        WDF_MEMORY_DESCRIPTOR Memory;
        WDF_REQUEST_SEND_OPTIONS Options;
        ULONG_PTR Bytes = 0;
        NTSTATUS Status;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&Memory, Buffer, sizeof(Buffer));
        WDF_REQUEST_SEND_OPTIONS_INIT(&Options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
        Options.Timeout = WDF_REL_TIMEOUT_IN_MS(500);
        Status = WdfIoTargetSendWriteSynchronously(Target, NULL, &Memory, NULL, &Options, &Bytes);
        if (!NT_SUCCESS(Status)) return Status;
        if (Bytes != sizeof(Buffer)) return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    return STATUS_SUCCESS;
}

#define SWFAN_HAVE_PMIC_LOCK 1
#define SWFAN_HAVE_CPU_LOCK 1
#define SWFAN_HAVE_SENSOR_SAMPLE 1
/* Lock order: SensorLock -> CpuLock -> BacklightLock (snapshot only) or PmicLock.
 * PmicLock alone serializes transfers, not a complete V/F transition. */
static VOID SwFanSensorLock(PSWFAN_DEVICE Dev)
{
    if (Dev->SensorLock != NULL) WdfWaitLockAcquire(Dev->SensorLock, NULL);
}

static VOID SwFanSensorUnlock(PSWFAN_DEVICE Dev)
{
    if (Dev->SensorLock != NULL) WdfWaitLockRelease(Dev->SensorLock);
}

static VOID SwFanCpuLock(PSWFAN_DEVICE Dev)
{ WdfWaitLockAcquire(Dev->CpuLock, NULL); }
static VOID SwFanCpuUnlock(PSWFAN_DEVICE Dev)
{ WdfWaitLockRelease(Dev->CpuLock); }

static VOID SwFanPmicLock(PSWFAN_DEVICE Dev)
{
    if (Dev->PmicLock != NULL) WdfWaitLockAcquire(Dev->PmicLock, NULL);
}

static VOID SwFanPmicUnlock(PSWFAN_DEVICE Dev)
{
    if (Dev->PmicLock != NULL) WdfWaitLockRelease(Dev->PmicLock);
}

static NTSTATUS SwFanSampleTemp(PSWFAN_DEVICE Dev, PULONG OutTemp)
{
    UCHAR Local = 0, Remote = 0;
    ULONG Temp;
    NTSTATUS Status;

    if (Dev->Stopping || Dev->Sensor == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    SwFanSensorLock(Dev);
    if (Dev->Stopping || Dev->Sensor == NULL) {
        SwFanSensorUnlock(Dev);
        return STATUS_DEVICE_NOT_READY;
    }
    Status = SwFanReadReg(Dev->Sensor, TMP451_REMOTE, &Remote);
    if (NT_SUCCESS(Status)) {
        Status = SwFanReadReg(Dev->Sensor, TMP451_LOCAL, &Local);
    }
    SwFanSensorUnlock(Dev);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Temp = (Remote > Local) ? Remote : Local;
    SwFanCpuLock(Dev);
    if (!Dev->Stopping) {
        Dev->LastTemp = Temp;
        Dev->TempValid = TRUE;
#ifdef SWITCH_LITE_GPU
        Dev->SocTempC = Remote;
        Dev->BoardTempC = Local;
        Dev->TempSampleTime = KeQueryInterruptTime();
#endif
    } else {
        Status = STATUS_DEVICE_NOT_READY;
    }
    SwFanCpuUnlock(Dev);
    if (NT_SUCCESS(Status) && OutTemp != NULL) {
        *OutTemp = Temp;
    }
    return Status;
}

static VOID SwFanSetDuty(PSWFAN_DEVICE Dev, ULONG Duty);
#ifdef SWITCH_LITE_GPU
#define SWFAN_HAVE_DFLL 1
static ULONG SwFanDfllReadKhz(PSWFAN_DEVICE Dev);
static NTSTATUS SwFanDfllStopLocked(PSWFAN_DEVICE Dev);
static VOID SwFanEmcCpuRateChanged(PSWFAN_DEVICE Dev, ULONG Khz);
static NTSTATUS SwFanDvfsUpdateLocked(PSWFAN_DEVICE Dev, ULONG Temp, BOOLEAN Valid);
#endif
#include "t214/clock.h"
#ifdef SWITCH_LITE_GPU
#include "t214/dfll_windows.h"
#include "t214/gpu_pmic.h"
#include "t214/emc_windows.h"
#include "integration/switch-lite/windows/SwitchFan/src/t214/host1x_clock.h"
#endif

/* ------------------------------------------------------------------- fan */

static VOID SwFanSetDuty(PSWFAN_DEVICE Dev, ULONG Duty)
{
    ULONG Csr;
    if (Duty > PWM_DUTY_MAX) {
        Duty = PWM_DUTY_MAX;
    }
    Csr = (Duty == 0) ? (PWM_CSR_EN | PWM_ABSOLUTE_OFF) : (PWM_CSR_EN | ((PWM_DUTY_MAX - Duty) << 16));
    WRITE_REGISTER_ULONG((PULONG)(Dev->Pwm + Dev->FanOffset), Csr);
    Dev->Duty = Duty;
}

static ULONG SwFanDutyForTemp(ULONG Temp, ULONG Previous)
{
    if (Temp >= 45) return 236;
    if (Temp >= 43) return 212;
    if (Temp >= 40) return 177;
    if (Temp >= 38) return 118;
    if (Temp >= 35) return 76;
    if (Temp <= 32) return 0;
    return (Previous > 76) ? 76 : Previous;    /* 33-34 C: hold (at most the 35 C step) */
}

VOID SwFanEvtTimer(WDFTIMER Timer)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(SwFanGetTimer(Timer)->Device);
    UCHAR Local = 0, Remote = 0;
    NTSTATUS Status, ClockStatus = STATUS_SUCCESS;
    BOOLEAN ScreenOff, ClockRestored;
    ULONG DisplayState, BacklightDuty;

    if (Dev->Stopping || Dev->TimerStopping) return;
    WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    ScreenOff = Dev->IsLite && Dev->BacklightReady && Dev->DisplayState == 0;
    DisplayState = Dev->DisplayState;
    BacklightDuty = SwBacklightDuty(Dev->Brightness, DisplayState);
    WdfWaitLockRelease(Dev->BacklightLock);
    SwFanTrace(L"DisplayState", DisplayState);
    SwFanTrace(L"BacklightDuty", BacklightDuty);
    /* Screen-off can remain in S0. Keep temperature polling, but release the
       optional CPU overclock before allowing the fan to stop. */
    if (ScreenOff) {
        ClockStatus = SwCpuApply(Dev, FALSE);
        SwFanTrace(L"CpuLastError", (ULONG)ClockStatus);
    }
    /* DFLL sampling writes MON_CTRL, so even this cooling snapshot belongs
       to the same CPU transaction lock as load hints and rate changes. */
    SwFanCpuLock(Dev);
    ClockRestored = Dev->CpuConfigured && !Dev->CpuApplied &&
                    NT_SUCCESS(ClockStatus) && SwCpuKhz(Dev) == Dev->BootCpuKhz;
    SwFanCpuUnlock(Dev);
    SwFanTrace(L"ScreenOffCooling", ScreenOff);
    SwFanSensorLock(Dev);
    Status = SwFanReadReg(Dev->Sensor, TMP451_REMOTE, &Remote);
    if (NT_SUCCESS(Status)) {
        Status = SwFanReadReg(Dev->Sensor, TMP451_LOCAL, &Local);
    }
    SwFanSensorUnlock(Dev);
    if (NT_SUCCESS(Status)) {
        ULONG Temp = (Remote > Local) ? Remote : Local;
        SwFanCpuLock(Dev);
        Dev->LastTemp = Temp;
        Dev->TempValid = TRUE;
#ifdef SWITCH_LITE_GPU
        Dev->SocTempC = Remote;
        Dev->BoardTempC = Local;
        Dev->TempSampleTime = KeQueryInterruptTime();
#endif
        SwFanCpuUnlock(Dev);
        Dev->Failures = 0;
        WdfWaitLockAcquire(Dev->FanLock, NULL);
        ULONG Duty = SwFanDutyForTemp(Temp, Dev->Duty);
        if (Dev->IsLite) {
            Dev->FanTargetPwm=sw_fan_profile_sample(&Dev->FanFilter,Dev->FanProfile,(int32_t)Temp*1000,1);
            Dev->FanProfileFlags=SWFAN_PROFILE_VALID | (Temp>=80 ? SWFAN_PROFILE_FAILSAFE : 0);
            Dev->FanSampleTime=KeQueryInterruptTime();
            Duty=sw_fan_pwm_duty(Dev->FanTargetPwm);
        }
        // Retain the overclock floor until stock CPU clock is confirmed. On
        // wake, restore cooling before SwCpuTemperature can reapply overclock.
        if (Dev->IsLite && Dev->Config.CpuKhz == LITE_CPU_KHZ &&
            ((!ScreenOff && Dev->CpuOverclockEnabled) || !ClockRestored) && Duty < 51) Duty = 51;
        SwFanSetDuty(Dev, Duty);
        WdfWaitLockRelease(Dev->FanLock);
        if (!ScreenOff) SwCpuTemperature(Dev, Temp, TRUE);
        SwFanTrace(L"TempSoC", Remote);
        SwFanTrace(L"TempBoard", Local);
    } else {
        SwFanCpuLock(Dev);
        Dev->TempValid = FALSE;
        SwFanCpuUnlock(Dev);
        SwFanTrace(L"LastSensorError", (ULONG)Status);
        /* A stopped fan needs cooling on the first lost temperature sample. */
        WdfWaitLockAcquire(Dev->FanLock, NULL);
        Dev->FanProfileFlags=SWFAN_PROFILE_FAILSAFE;
        Dev->FanTargetPwm=255;
        Dev->FanFilter.initialized=0;
        const BOOLEAN FailCooling=++Dev->Failures >= 3 || Dev->Duty == 0;
        if (FailCooling || Dev->IsLite) SwFanSetDuty(Dev, Dev->IsLite ? PWM_DUTY_MAX : FAILSAFE_DUTY);
        WdfWaitLockRelease(Dev->FanLock);
        if (FailCooling) {
            SwCpuTemperature(Dev, 0, FALSE);
        }
    }
    SwFanTrace(L"Duty", Dev->Duty);
    SwFanTrace(L"SensorFailures", Dev->Failures);
    SwFanCpuLock(Dev);
    if (Dev->Car != NULL) SwFanTrace(L"CpuActualKhz", SwCpuKhz(Dev));
    SwFanCpuUnlock(Dev);
    SwFanTrace(L"Runs", (ULONG)InterlockedIncrement(&Dev->Runs));
    {
        KIRQL irql;
        KeAcquireSpinLock(&Dev->TimerLock, &irql);
        if (!Dev->Stopping && !Dev->TimerStopping) WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
        KeReleaseSpinLock(&Dev->TimerLock, irql);
    }
}

/* ------------------------------------------------------------- backlight */

static ULONG SwFanReadParam(PCWSTR Name, ULONG Default)
{
    ULONG Value = Default;
    RTL_QUERY_REGISTRY_TABLE Table[2];

    if (SwFanParamsPath[0] == L'\0') {
        return Default;
    }
    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = (PWSTR)Name;
    Table[0].EntryContext = &Value;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath, Table, NULL, NULL))) {
        Value = Default;
    }
    return Value;
}

/* CSR_0 from the brightness and the display state. Caller holds BacklightLock. */
static ULONG SwFanApplyBacklight(PSWFAN_DEVICE Dev)
{
    ULONG Duty = SwBacklightDuty(Dev->Brightness, Dev->DisplayState);
    if (Dev->BacklightPwm != NULL) {
        PULONG Csr = (PULONG)(Dev->BacklightPwm + PWM_CSR_0);
        WRITE_REGISTER_ULONG(Csr, SwBacklightCsr(READ_REGISTER_ULONG(Csr), Duty));
    }
    return Duty;
}

NTSTATUS SwFanDisplayStateCallback(LPCGUID Setting, PVOID Value, ULONG Length, PVOID Context)
{
    PSWFAN_DEVICE Dev = (PSWFAN_DEVICE)Context;
    if (Length >= sizeof(ULONG) && IsEqualGUID(Setting, &SwFanDisplayStateGuid)) {
        WdfWaitLockAcquire(Dev->BacklightLock, NULL);
        Dev->DisplayState = *(PULONG)Value;
        /* Power notification must not wait for registry/storage I/O. The
           existing temperature timer records this snapshot after the lock. */
        (VOID)SwFanApplyBacklight(Dev);
        WdfWaitLockRelease(Dev->BacklightLock);
    }
    return STATUS_SUCCESS;
}

VOID SwFanEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutLength, size_t InLength, ULONG Code)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(WdfIoQueueGetDevice(Queue));
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR Info = 0;
    ULONG SavedBrightness = 0;
    UNREFERENCED_PARAMETER(OutLength);
    UNREFERENCED_PARAMETER(InLength);

    if (Code == IOCTL_SWFAN_GET_FAN_PROFILE || Code == IOCTL_SWFAN_SET_FAN_PROFILE) {
        if (!Dev->IsLite || !Dev->Pwm || Dev->Stopping || Dev->TimerStopping) {
            WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
            return;
        }
        if (Code == IOCTL_SWFAN_SET_FAN_PROFILE) {
            SWFAN_PROFILE_REQUEST *In;
            Status=WdfRequestRetrieveInputBuffer(Request,sizeof(*In),(PVOID *)&In,NULL);
            if (NT_SUCCESS(Status)) {
                if (In->Version!=SWFAN_PROFILE_VERSION) Status=STATUS_REVISION_MISMATCH;
                else if (In->Mode>=SwFanProfileCount) Status=STATUS_INVALID_PARAMETER;
                else {
                    WdfWaitLockAcquire(Dev->FanLock,NULL);
                    Dev->FanProfile=In->Mode;
                    WdfWaitLockRelease(Dev->FanLock);
                }
            }
        } else {
            SWFAN_PROFILE_STATUS *Out;
            Status=WdfRequestRetrieveOutputBuffer(Request,sizeof(*Out),(PVOID *)&Out,NULL);
            if (NT_SUCCESS(Status)) {
                WdfWaitLockAcquire(Dev->FanLock,NULL);
                RtlZeroMemory(Out,sizeof(*Out));
                Out->Version=SWFAN_PROFILE_VERSION; Out->Mode=Dev->FanProfile;
                Out->Duty=Dev->Duty; Out->TargetPwm=Dev->FanTargetPwm;
                Out->Flags=Dev->FanProfileFlags; Out->FilteredTempMc=Dev->FanFilter.lta;
                ULONGLONG age=Dev->FanSampleTime ?
                    (KeQueryInterruptTime()-Dev->FanSampleTime)/10000u : MAXULONG;
                Out->SampleAgeMs=age>MAXULONG ? MAXULONG : (ULONG)age;
                WdfWaitLockRelease(Dev->FanLock);
                Info=sizeof(*Out);
            }
        }
        WdfRequestCompleteWithInformation(Request,Status,Info);
        return;
    }

#ifdef SWITCH_LITE_GPU
    if (Code == TG_HOST1X_IOCTL_CLOCK_HOLD) {
        SwFanHost1xDispatch(WdfIoQueueGetDevice(Queue), Request);
        return;
    }
    if (Code == IOCTL_SWFAN_GET_EMC_STATUS) {
        SWFAN_EMC_STATUS *Out;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID *)&Out, NULL);
        if (NT_SUCCESS(Status)) {
            SwFanGetEmcStatus(Dev, Out);
            Info = sizeof(*Out);
        }
        WdfRequestCompleteWithInformation(Request, Status, Info);
        return;
    }
    if (Code == TG_GPU_IOCTL_PMIC) {
        Status = SwFanGpuPmic(Dev, Request, &Info);
        WdfRequestCompleteWithInformation(Request, Status, Info);
        return;
    }
    if (Code == IOCTL_SWFAN_GET_POWER_STATUS) {
        SWFAN_POWER_STATUS *Out;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID *)&Out, NULL);
        if (NT_SUCCESS(Status)) {
            SwFanGetPowerStatus(Dev, Out);
            Info = sizeof(*Out);
        }
        WdfRequestCompleteWithInformation(Request, Status, Info);
        return;
    }
    if (Code == IOCTL_SWFAN_SET_POWER_MODE) {
        SWFAN_POWER_REQUEST *In;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID *)&In, NULL);
        if (NT_SUCCESS(Status)) {
            Status = In->Version == SWFAN_POWER_STATUS_VERSION ?
                SwFanSetPowerMode(Dev, In->Mode) : STATUS_REVISION_MISMATCH;
        }
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return;
    }
    if (Code == IOCTL_SWFAN_CPU_LOAD) {
        SWFAN_CPU_LOAD *In;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID *)&In, NULL);
        if (NT_SUCCESS(Status)) {
            if (In->Version != SWFAN_POWER_STATUS_VERSION || In->BusyPermille > 1000u)
                Status = STATUS_INVALID_PARAMETER;
            else {
                BOOLEAN ScreenOff;
                SwFanCpuLock(Dev);
                Dev->Dfll.LoadPermille = In->BusyPermille;
                Dev->Dfll.LoadTime = KeQueryInterruptTime();
                WdfWaitLockAcquire(Dev->BacklightLock, NULL);
                ScreenOff = Dev->BacklightReady && Dev->DisplayState == 0;
                WdfWaitLockRelease(Dev->BacklightLock);
                if (!Dev->Stopping && !ScreenOff && Dev->PowerMode < SwNvpCount)
                    Status = SwFanDvfsUpdateLocked(Dev, Dev->LastTemp, Dev->TempValid);
                else Status = STATUS_SUCCESS;
                SwFanCpuUnlock(Dev);
            }
        }
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return;
    }
#endif
    WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    if (Dev->BacklightPwm == NULL) {
        Status = STATUS_NOT_SUPPORTED;
    } else if (Code == IOCTL_SWBL_GET) {
        SWBL_STATUS *Out;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID *)&Out, NULL);
        if (NT_SUCCESS(Status)) {
            Out->Version = 2;
            Out->Brightness = Dev->Brightness;
            Out->Duty = (READ_REGISTER_ULONG((PULONG)(Dev->BacklightPwm + PWM_CSR_0)) >> 16) & 0x7FFF;
            Out->DisplayState = Dev->DisplayState;
            Info = sizeof(*Out);
        }
    } else if (Code == IOCTL_SWBL_SET) {
        PULONG In;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID *)&In, NULL);
        if (NT_SUCCESS(Status)) {
            ULONG Level = *In;
            Dev->Brightness = SwBacklightClamp(Level);
            SavedBrightness = Dev->Brightness;
            (VOID)SwFanApplyBacklight(Dev);
        }
    }
    WdfWaitLockRelease(Dev->BacklightLock);
    /* The sequential queue preserves setter order without holding the
       display-notification lock while a registry hive is written. */
    if (SavedBrightness != 0) SwFanTrace(L"Brightness", SavedBrightness);
    WdfRequestCompleteWithInformation(Request, Status, Info);
}

/* ------------------------------------------------------------------- PnP */

NTSTATUS SwFanEvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    Dev->TempValid = FALSE;
    Dev->FanProfileFlags = 0;
    Dev->FanFilter.initialized = 0;
    /* Cooling resumes before the passive timer can restore CPU overclock. */
    if (Dev->IsLite && Dev->Pwm != NULL) SwFanSetDuty(Dev, 76);
    SwFanTrace(L"SleepFanOff", 0);
    return STATUS_SUCCESS;
}

NTSTATUS SwFanEvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    POWER_ACTION Action = WdfDeviceGetSystemPowerAction(Device);
    BOOLEAN Sleeping = Action == PowerActionSleep || Action == PowerActionHibernate;
    BOOLEAN Stock = Dev->Config.CpuKhz != LITE_CPU_KHZ ||
                    (Dev->CpuConfigured && !Dev->CpuApplied && SwCpuKhz(Dev) == Dev->BootCpuKhz);
    /* SelfManagedIoSuspend has drained the timer and released CPU overclock.
       Device removal, shutdown and S0 device idle keep the existing cooling.
       Only a cool system sleep transition can stop polling with the fan off. */
    if (Dev->IsLite && Dev->Pwm != NULL && TargetState != WdfPowerDeviceD3Final && Sleeping && Stock &&
        Dev->TempValid && Dev->LastTemp <= 39) {
        SwFanSetDuty(Dev, 0);
        SwFanTrace(L"SleepFanOff", 1);
    }
    return STATUS_SUCCESS;
}

NTSTATUS SwFanEvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    ULONG Count = WdfCmResourceListGetCount(Translated), Index;
    BOOLEAN HaveSensor = FALSE;
    Dev->ConnectionCount = 0;
#ifdef SWITCH_LITE_GPU
    Dev->Emc.MailResources = 0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR actmonRaw=NULL,actmonTranslated=NULL;
#endif
    UNREFERENCED_PARAMETER(Raw);

    for (Index = 0; Index < Count; ++Index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = WdfCmResourceListGetDescriptor(Translated, Index);
        if (Desc == NULL) {
            continue;
        }
#ifdef SWITCH_LITE_GPU
        if (Desc->Type==CmResourceTypeInterrupt) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR raw=WdfCmResourceListGetDescriptor(Raw,Index);
            /* T210 DT SPI45 is GIC INTID/ACPI GSIV77 (32 SPI base +45).
             * Use PnP's translated vector, never pass the raw number to HAL. */
            if (actmonRaw || !raw || raw->Type!=CmResourceTypeInterrupt ||
                raw->u.Interrupt.Vector!=77u ||
                (raw->Flags&(CM_RESOURCE_INTERRUPT_LATCHED|CM_RESOURCE_INTERRUPT_MESSAGE)) ||
                (Desc->Flags&(CM_RESOURCE_INTERRUPT_LATCHED|CM_RESOURCE_INTERRUPT_MESSAGE)))
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            actmonRaw=raw;actmonTranslated=Desc;
            continue;
        }
#endif
        if (Desc->Type == CmResourceTypeMemory) {
            if (Desc->u.Memory.Start.QuadPart == 0x7000A010 && Desc->u.Memory.Length == sizeof(ULONG)) {
                Dev->Pwm = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, sizeof(ULONG), PAGE_READWRITE | PAGE_NOCACHE);
                if (Dev->Pwm == NULL) return STATUS_INSUFFICIENT_RESOURCES;
                Dev->PwmLength = sizeof(ULONG);
                Dev->FanOffset = 0; Dev->IsLite = TRUE;
            } else if (Desc->u.Memory.Start.QuadPart == 0x7000A000 && Desc->u.Memory.Length == sizeof(ULONG)) {
                Dev->BacklightPwm = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, sizeof(ULONG), PAGE_READWRITE | PAGE_NOCACHE);
                if (Dev->BacklightPwm == NULL) return STATUS_INSUFFICIENT_RESOURCES;
                Dev->BacklightLength = sizeof(ULONG);
            } else if (Desc->u.Memory.Start.QuadPart == 0x7000A000 && Desc->u.Memory.Length >= 0x20) {
                Dev->Pwm = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, Desc->u.Memory.Length, PAGE_READWRITE | PAGE_NOCACHE);
                if (Dev->Pwm == NULL) return STATUS_INSUFFICIENT_RESOURCES;
                Dev->PwmLength = Desc->u.Memory.Length;
                Dev->FanOffset = 0x10; Dev->IsLite = FALSE;
            } else if (Desc->u.Memory.Start.QuadPart == 0x60006000 && Desc->u.Memory.Length == 0x100) {
                Dev->Car = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, 0x100, PAGE_READWRITE | PAGE_NOCACHE);
                if (Dev->Car == NULL) return STATUS_INSUFFICIENT_RESOURCES;
            } else if (Desc->u.Memory.Start.QuadPart == LITE_PERF_ADDRESS && Desc->u.Memory.Length == 0x1000) {
                volatile ULONG *Map = (volatile ULONG *)MmMapIoSpaceEx(Desc->u.Memory.Start, 0x1000, PAGE_READONLY | PAGE_NOCACHE);
                ULONG I;
                if (Map == NULL) return STATUS_INSUFFICIENT_RESOURCES;
                for (I = 0; I < sizeof(Dev->Config)/sizeof(ULONG); ++I)
                    ((PULONG)&Dev->Config)[I] = READ_REGISTER_ULONG((PULONG)&Map[I]);
#ifdef SWITCH_LITE_GPU
                for (I = 0; I < sizeof(Dev->Dfll.Handoff)/sizeof(ULONG); ++I)
                    ((PULONG)&Dev->Dfll.Handoff)[I] = READ_REGISTER_ULONG((PULONG)&Map[0x100/4 + I]);
                for (I = 0; I < sizeof(Dev->Emc.Handoff)/sizeof(ULONG); ++I)
                    ((PULONG)&Dev->Emc.Handoff)[I] = READ_REGISTER_ULONG((PULONG)&Map[0x200/4 + I]);
#endif
                MmUnmapIoSpace((PVOID)Map, 0x1000);
                if (Dev->Config.Magic != LITE_PERF_MAGIC || Dev->Config.Version != LITE_PERF_VERSION)
                    RtlZeroMemory(&Dev->Config, sizeof(Dev->Config));
            }
#ifdef SWITCH_LITE_GPU
            if (Desc->u.Memory.Start.QuadPart == 0x60001000 && Desc->u.Memory.Length == 12) {
                Dev->Emc.Semaphore = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, 12, PAGE_READWRITE | PAGE_NOCACHE);
                if (!Dev->Emc.Semaphore) return STATUS_INSUFFICIENT_RESOURCES;
            } else if (Desc->u.Memory.Start.QuadPart == 0x60004018 && Desc->u.Memory.Length == 4) {
                Dev->Emc.Doorbell = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start, 4, PAGE_READWRITE | PAGE_NOCACHE);
                if (!Dev->Emc.Doorbell) return STATUS_INSUFFICIENT_RESOURCES;
            } else if (Desc->u.Memory.Start.QuadPart == LITE_EMC_MAIL_BASE && Desc->u.Memory.Length == LITE_EMC_MAIL_SIZE) {
                Dev->Emc.MailResources |= LITE_EMC_REGION_DRAM;
            } else if (Desc->u.Memory.Start.QuadPart == LITE_EMC_IRAM_BASE && Desc->u.Memory.Length == LITE_EMC_IRAM_SIZE) {
                Dev->Emc.MailResources |= LITE_EMC_REGION_IRAM;
            }
            for (ULONG I=0;I<SwActmonMapCount;++I) {
                const UINT64 base=(UINT64)Desc->u.Memory.Start.QuadPart;
                const UINT64 address=SwActmonResources[I].address;
                const ULONG length=SwActmonResources[I].length;
                /* V-bank gate status (0x360) is already authorized within
                 * the DFLL status resource (0x35c, 12 bytes). Map just its
                 * register, with its own balanced mapping lifetime. */
                if (!Dev->Emc.ActmonMap[I] && base<=address &&
                    address-base<=Desc->u.Memory.Length &&
                    length<=Desc->u.Memory.Length-(address-base)) {
                    PHYSICAL_ADDRESS start; start.QuadPart=(LONGLONG)address;
                    Dev->Emc.ActmonMap[I]=(volatile UCHAR *)MmMapIoSpaceEx(start,length,
                        (SwActmonResources[I].read_only?PAGE_READONLY:PAGE_READWRITE)|PAGE_NOCACHE);
                    if (!Dev->Emc.ActmonMap[I]) return STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            for (ULONG I=0;I<SwQosRegisterCount;++I) {
                if (Desc->u.Memory.Start.QuadPart==SwEmcQosResources[I] && Desc->u.Memory.Length==4) {
                    if (Dev->Emc.QosMap[I]) return STATUS_DEVICE_CONFIGURATION_ERROR;
                    if (I<=SwQosRingMax && (Desc->Flags&CM_RESOURCE_MEMORY_READ_ONLY)) break;
                    Dev->Emc.QosMap[I]=(volatile ULONG *)MmMapIoSpaceEx(Desc->u.Memory.Start,4,
                        (I<=SwQosRingMax?PAGE_READWRITE:PAGE_READONLY)|PAGE_NOCACHE);
                    if (!Dev->Emc.QosMap[I]) return STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
            }
            for (ULONG I=0;I<SW_EMC_PTSA_RESOURCE_COUNT;++I) {
                if (Desc->u.Memory.Start.QuadPart!=0x70019000u+sw_emc_ptsa_resources[I].offset ||
                    Desc->u.Memory.Length!=4) continue;
                /* A read-only legacy CRS is not authorization to initialize MC.
                 * Missing/wrong permissions keep SET_RATE gated in bootstrap. */
                if (Dev->Emc.PtsaMap[I]) return STATUS_DEVICE_CONFIGURATION_ERROR;
                if (sw_emc_ptsa_resources[I].writable && (Desc->Flags&CM_RESOURCE_MEMORY_READ_ONLY))
                    continue;
                Dev->Emc.PtsaMap[I]=(volatile ULONG *)MmMapIoSpaceEx(Desc->u.Memory.Start,4,
                    (sw_emc_ptsa_resources[I].writable?PAGE_READWRITE:PAGE_READONLY)|PAGE_NOCACHE);
                if (!Dev->Emc.PtsaMap[I]) return STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            for (ULONG I = 0; I < SwDfllResourceCount; ++I) {
                if (Desc->u.Memory.Start.QuadPart == SwDfllResources[I].Address &&
                    Desc->u.Memory.Length == SwDfllResources[I].Length) {
                    Dev->Dfll.Map[I] = (volatile UCHAR *)MmMapIoSpaceEx(Desc->u.Memory.Start,
                        SwDfllResources[I].Length,
                        (SwDfllResources[I].ReadOnly ? PAGE_READONLY : PAGE_READWRITE) | PAGE_NOCACHE);
                    if (!Dev->Dfll.Map[I]) return STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
            }
#endif
        } else if (Desc->Type == CmResourceTypeConnection &&
                   Desc->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
                   Desc->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C) {
            LARGE_INTEGER Id;
            Id.LowPart = Desc->u.Connection.IdLowPart;
            Id.HighPart = (LONG)Desc->u.Connection.IdHighPart;
            if (Dev->ConnectionCount == 0) { Dev->SensorId = Id; HaveSensor = TRUE; }
            else if (Dev->ConnectionCount <= 2) Dev->PmicIds[Dev->ConnectionCount - 1] = Id;
            ++Dev->ConnectionCount;
        }
    }
    if (Dev->Pwm == NULL || !HaveSensor) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
#ifdef SWITCH_LITE_GPU
    /* Resource order is not an ABI. Map the mailbox only after both its
     * reserved range and the complete firmware handoff have been collected. */
    if (LiteEmcHandoffValid(&Dev->Emc.Handoff) &&
        (Dev->Emc.MailResources & LiteEmcMailRegion(Dev->Emc.Handoff.MailAddress))) {
        PHYSICAL_ADDRESS address;
        address.QuadPart = Dev->Emc.Handoff.MailAddress;
        Dev->Emc.Mail = (volatile UCHAR *)MmMapIoSpaceEx(address, 128, PAGE_READWRITE | PAGE_NOCACHE);
        if (!Dev->Emc.Mail) return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (actmonRaw && Dev->IsLite) {
        WDF_INTERRUPT_CONFIG config;
        WDF_INTERRUPT_CONFIG_INIT(&config,SwFanEmcIsr,NULL);
        config.AutomaticSerialization=FALSE;
        config.InterruptRaw=actmonRaw;config.InterruptTranslated=actmonTranslated;
        config.EvtInterruptEnable=SwFanEmcInterruptEnable;
        config.EvtInterruptDisable=SwFanEmcInterruptDisable;
        config.ReportInactiveOnPowerDown=WdfFalse;
        NTSTATUS status=WdfInterruptCreate(Device,&config,WDF_NO_OBJECT_ATTRIBUTES,&Dev->Emc.Interrupt);
        if (!NT_SUCCESS(status)) return status;
    }
#endif
    if (!Dev->IsLite) Dev->BacklightPwm = Dev->Pwm;
    /* Read once per hardware start so a diagnostic does not introduce another
       live clock transition. Zero keeps the inherited CPU clock across lock. */
    Dev->CpuOverclockEnabled = SwFanReadParam(L"CpuOverclockEnabled", 1) != 0;
    Dev->BaseCpuOverclockEnabled = Dev->CpuOverclockEnabled;
    Dev->PowerMode = 7; /* separate Windows recovery baseline; never auto-select OC */
    Dev->PolicySeq = 0;
    Dev->FanProfile = SwFanProfileHandheld;
    RtlZeroMemory(&Dev->FanFilter,sizeof(Dev->FanFilter));
    Dev->FanProfileFlags=0; Dev->FanSampleTime=0;
#ifdef SWITCH_LITE_GPU
    (VOID)InterlockedExchange(&Dev->GpuDvfsPolicy, (LONG)TG_GPU_DVFS_POLICY_PACK(0, 460800u, 460800u, 0));
#endif
    SwFanTrace(L"CpuOverclockEnabled", Dev->CpuOverclockEnabled);
    SwFanTrace(L"BacklightAvailable", Dev->BacklightPwm != NULL);
    SwFanTrace(L"CpuHandoffKhz", Dev->Config.CpuKhz);
    SwFanTrace(L"CpuRequestedKhz", Dev->IsLite && Dev->Config.CpuKhz == LITE_CPU_KHZ ?
               CPU_TARGET_KHZ : Dev->Config.CpuKhz);
    SwFanTrace(L"RamRequestedKhz", Dev->Config.RamKhz);
    SwFanTrace(L"RamActualKhz", Dev->Config.RamActualKhz);
    SwFanTrace(L"HandoffStatus", Dev->Config.Status);
    SwFanTrace(L"PmicAddress", Dev->Config.PmicAddress);
    SwFanTrace(L"CoreMv", Dev->Config.CoreMv);
    SwFanTrace(L"BpmpError", Dev->Config.BpmpError);
    SwFanTrace(L"BpmpChannel", Dev->Config.BpmpChannel);
#ifdef SWITCH_LITE_GPU
    /* Publish the already-read startup contract once, so missing firmware
       handoff is distinguishable from an unassigned ACPI resource or a busy
       mailbox. This does not send a BPMP request or change channel tokens. */
    SwFanTrace(L"EmcHandoffMagic", Dev->Emc.Handoff.Magic);
    SwFanTrace(L"EmcHandoffVersion", Dev->Emc.Handoff.Version);
    SwFanTrace(L"EmcHandoffValid", LiteEmcHandoffValid(&Dev->Emc.Handoff));
    SwFanTrace(L"EmcHandoffChannel", Dev->Emc.Handoff.Channel);
    SwFanTrace(L"EmcHandoffMail", Dev->Emc.Handoff.MailAddress);
    SwFanTrace(L"EmcHandoffGpuSpeedo", Dev->Emc.Handoff.GpuSpeedo);
    SwFanTrace(L"EmcHandoffSocSpeedo", Dev->Emc.Handoff.SocSpeedo);
    SwFanTrace(L"EmcResourceMask", Dev->Emc.MailResources |
        (Dev->Emc.Semaphore ? 4u : 0u) | (Dev->Emc.Doorbell ? 8u : 0u) |
        (Dev->Emc.Mail ? 16u : 0u));
    SwFanTrace(L"EmcTokenAtStart", Dev->Emc.Semaphore ?
        READ_REGISTER_ULONG((PULONG)Dev->Emc.Semaphore) : MAXULONG);
#endif
    SwFanTrace(L"CsrAtStart", READ_REGISTER_ULONG((PULONG)(Dev->Pwm + Dev->FanOffset)));
    return STATUS_SUCCESS;
}

NTSTATUS SwFanEvtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Translated)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    NTSTATUS Status = SwFanEvtSuspend(Device);
    UNREFERENCED_PARAMETER(Translated);
    /* ReleaseHardware cannot veto teardown. Always unregister callbacks and
       unmap resources, even if restoring PLLX failed. Do not reset DFLL or
       lower M4 here; propagate the failed restoration instead. */
    /* Power notifications run outside the WDF queue: drain them before unmap. */
    if (Dev->DisplayCallback != NULL) {
        (VOID)PoUnregisterPowerSettingCallback(Dev->DisplayCallback);
        Dev->DisplayCallback = NULL;
    }
    WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    if (Dev->BacklightReady) {
        Dev->DisplayState = 1;
        (VOID)SwFanApplyBacklight(Dev);
    }
    if (Dev->BacklightLength != 0 && Dev->BacklightPwm != NULL)
        MmUnmapIoSpace((PVOID)Dev->BacklightPwm, Dev->BacklightLength);
    Dev->BacklightPwm = NULL;
    Dev->BacklightLength = 0;
    Dev->BacklightReady = FALSE;
    WdfWaitLockRelease(Dev->BacklightLock);
    if (Dev->Pwm != NULL) {
        MmUnmapIoSpace((PVOID)Dev->Pwm, Dev->PwmLength);
        Dev->Pwm = NULL;
    }
    if (Dev->Car != NULL) { MmUnmapIoSpace((PVOID)Dev->Car, 0x100); Dev->Car = NULL; }
#ifdef SWITCH_LITE_GPU
    /* Power-down has disconnected this IRQ; queued DPC/work was joined by
     * Suspend. Delete before zeroing its state or unmapping its MMIO. */
    if (Dev->Emc.Interrupt) WdfObjectDelete(Dev->Emc.Interrupt);
    if (Dev->Emc.CoreTarget) { WdfIoTargetClose(Dev->Emc.CoreTarget); WdfObjectDelete(Dev->Emc.CoreTarget); }
    if (Dev->Emc.Mail) MmUnmapIoSpace((PVOID)Dev->Emc.Mail, 128);
    if (Dev->Emc.Doorbell) MmUnmapIoSpace((PVOID)Dev->Emc.Doorbell, 4);
    if (Dev->Emc.Semaphore) MmUnmapIoSpace((PVOID)Dev->Emc.Semaphore, 12);
    for (ULONG I=0;I<SwActmonMapCount;++I)
        if (Dev->Emc.ActmonMap[I]) MmUnmapIoSpace((PVOID)Dev->Emc.ActmonMap[I],SwActmonResources[I].length);
    for (ULONG I=0;I<SwQosRegisterCount;++I)
        if (Dev->Emc.QosMap[I]) MmUnmapIoSpace((PVOID)Dev->Emc.QosMap[I],4);
    for (ULONG I=0;I<SW_EMC_PTSA_RESOURCE_COUNT;++I)
        if (Dev->Emc.PtsaMap[I]) MmUnmapIoSpace((PVOID)Dev->Emc.PtsaMap[I],4);
    RtlZeroMemory(&Dev->Emc, sizeof(Dev->Emc));
    for (ULONG I = 0; I < SwDfllResourceCount; ++I) {
        if (Dev->Dfll.Map[I]) MmUnmapIoSpace((PVOID)Dev->Dfll.Map[I], SwDfllResources[I].Length);
        Dev->Dfll.Map[I] = NULL;
    }
#endif
    return Status;
}

static VOID SwFanStartBacklight(WDFDEVICE Device)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    NTSTATUS Status;
    ULONG BootCsr, DefaultLevel;
    if (Dev->BacklightPwm == NULL || Dev->BacklightReady) return;
    WdfWaitLockAcquire(Dev->BacklightLock, NULL);
    BootCsr = READ_REGISTER_ULONG((PULONG)(Dev->BacklightPwm + PWM_CSR_0));
    DefaultLevel = Dev->IsLite ? ((BootCsr >> 16) & 0x7FFF) : BACKLIGHT_MAX;
    if (DefaultLevel < BACKLIGHT_MIN || DefaultLevel > BACKLIGHT_MAX) DefaultLevel = 150;
    Dev->Brightness = SwFanReadParam(L"Brightness", DefaultLevel);
    if (Dev->Brightness < BACKLIGHT_MIN || Dev->Brightness > BACKLIGHT_MAX) {
        Dev->Brightness = DefaultLevel;
    }
    Dev->DisplayState = 1;
    Dev->BacklightReady = TRUE;
    SwFanTrace(L"BacklightCsrAtStart", BootCsr);
    SwFanTrace(L"Brightness", Dev->Brightness);
    (VOID)SwFanApplyBacklight(Dev);
    WdfWaitLockRelease(Dev->BacklightLock);
    if (Dev->DisplayCallback == NULL) {
        /* Called once right away with the current state, so not under BacklightLock. */
        Status = PoRegisterPowerSettingCallback(WdfDeviceWdmGetDeviceObject(Device), &SwFanDisplayStateGuid,
                                                SwFanDisplayStateCallback, Dev, &Dev->DisplayCallback);
        SwFanTrace(L"DisplayCallbackStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            Dev->DisplayCallback = NULL;
        }
    }

}

NTSTATUS SwFanEvtSelfManagedIoInit(WDFDEVICE Device)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    NTSTATUS Status;
    /* Backlight first: it must not depend on the temperature sensor. */
    SwFanStartBacklight(Device);
    Status = SwFanOpenTarget(Device, Dev->SensorId, &Dev->Sensor);
    SwFanTrace(L"OpenSensorStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        SwFanSetDuty(Dev, FAILSAFE_DUTY);
        return Status;
    }
    Dev->Duty = 76;                 /* what UEFI R366+ set */
    if (Dev->Timer == NULL) {
        WDF_TIMER_CONFIG TimerConfig;
        WDF_OBJECT_ATTRIBUTES Attributes;
        WDF_TIMER_CONFIG_INIT(&TimerConfig, SwFanEvtTimer);
        TimerConfig.AutomaticSerialization = FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SWFAN_TIMER);
        Attributes.ParentObject = Device;
        Attributes.ExecutionLevel = WdfExecutionLevelPassive;
        Status = WdfTimerCreate(&TimerConfig, &Attributes, &Dev->Timer);
        SwFanTrace(L"TimerCreateStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            Dev->Timer = NULL;
            return Status;
        }
        SwFanGetTimer(Dev->Timer)->Device = Device;
    }
#ifdef SWITCH_LITE_GPU
    if (Dev->IsLite && !Dev->EmcTimer) {
        WDF_TIMER_CONFIG config;
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_TIMER_CONFIG_INIT(&config,SwFanEvtEmcTimer);
        config.AutomaticSerialization=FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject=Device; attributes.ExecutionLevel=WdfExecutionLevelPassive;
        Status=WdfTimerCreate(&config,&attributes,&Dev->EmcTimer);
        if (!NT_SUCCESS(Status)) return Status;
    }
#endif
    return SwFanEvtRestart(Device);
}

NTSTATUS SwFanEvtSuspend(WDFDEVICE Device)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    NTSTATUS Status;
    KIRQL irql;
    KeAcquireSpinLock(&Dev->TimerLock, &irql);
    Dev->TimerStopping = TRUE;
    KeReleaseSpinLock(&Dev->TimerLock, irql);
#ifdef SWITCH_LITE_GPU
    SW_EMC_IRQ_REQUEST mask={.Operation=SwEmcIrqMask};
    if (Dev->Emc.Interrupt) (VOID)SwEmcIrqRequest(&Dev->Emc,&mask);
#endif
    if (Dev->Timer != NULL) WdfTimerStop(Dev->Timer, TRUE);
#ifdef SWITCH_LITE_GPU
    if (Dev->EmcTimer) WdfTimerStop(Dev->EmcTimer,TRUE);
    if (Dev->EmcDpc) (VOID)WdfDpcCancel(Dev->EmcDpc,TRUE);
    if (Dev->EmcWorkItem) WdfWorkItemFlush(Dev->EmcWorkItem);
    WdfWaitLockAcquire(Dev->EmcLock,NULL);
    if (Dev->Emc.Active) {
        Dev->Emc.StopStatus = SwFanEmcStopLoad(Dev);
        Dev->Emc.Active = FALSE;
    }
    NTSTATUS emcStatus = Dev->Emc.StopStatus;
    WdfWaitLockRelease(Dev->EmcLock);
    KeAcquireSpinLock(&Dev->TimerLock, &irql);
    Dev->Stopping = TRUE;
    KeReleaseSpinLock(&Dev->TimerLock, irql);
    SwFanCpuLock(Dev);
    Dev->Emc.CpuFloorInitialized = FALSE;
    Dev->Emc.FloorsInitialized = FALSE;
    Dev->Emc.SampleTime = 0;
    Dev->Emc.Published.ActualKhz = 0;
    Dev->Emc.Published.Flags &= ~SWFAN_EMC_RATE_VALID;
    SwFanCpuUnlock(Dev);
#else
    KeAcquireSpinLock(&Dev->TimerLock, &irql);
    Dev->Stopping = TRUE;
    KeReleaseSpinLock(&Dev->TimerLock, irql);
#endif
    Status = SwCpuApply(Dev, FALSE);
#ifdef SWITCH_LITE_GPU
    if (NT_SUCCESS(Status) && !NT_SUCCESS(emcStatus)) Status=emcStatus;
#endif
    if (Dev->Pwm != NULL) SwFanSetDuty(Dev, 76);
    return Status;
}

NTSTATUS SwFanEvtRestart(WDFDEVICE Device)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    KIRQL irql;
    NTSTATUS Status = Dev->IsLite ? SwFanConfigureSensor(Dev->Sensor) : STATUS_SUCCESS;
    SwFanStartBacklight(Device);
    SwFanTrace(L"ConfigureSensorStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        SwFanSetDuty(Dev, FAILSAFE_DUTY);
        return Status;
    }
    if (Dev->IsLite) {
#ifdef SWITCH_LITE_GPU
        /* A driver rebind need not reset DRAM to the firmware's boot rate.
         * Until BPMP supplies the live rate, retain the CPU floor required
         * at the characterized 3200-MHz maximum (process0: 620 mV). */
        if (!Dev->Emc.CpuFloorInitialized)
            Dev->Dfll.EmcFloorUv=Dev->Dfll.Handoff.CpuProcessId==0u ? 620000u : 600000u;
        if (!Dev->Emc.FloorsInitialized && LiteEmcHandoffValid(&Dev->Emc.Handoff)) {
            uint32_t gpu_uv=tg_gpu_emc_floor_policy_uv(Dev->Emc.Handoff.GpuSpeedo,SW_EMC_CHARACTERIZED_MAX_KHZ);
            if (gpu_uv!=UINT32_MAX) {
                SwFanPmicLock(Dev);
                if (!Dev->EmcGpuVote.released) {
                    Dev->EmcGpuVote.guard_uv=gpu_uv;
                    (VOID)tg_emc_gpu_request(&Dev->EmcGpuVote,gpu_uv,FALSE);
                }
                SwFanPmicUnlock(Dev);
            }
        }
        Dev->Emc.Active = TRUE;
        Dev->Emc.StopStatus = STATUS_SUCCESS;
#endif
        SwCpuPrepare(Device);
#ifdef SWITCH_LITE_GPU
        SwFanEmcCpuRateChanged(Dev,Dev->BootCpuKhz);
#endif
    }
    Dev->Failures = 0;
    KeAcquireSpinLock(&Dev->TimerLock, &irql);
    Dev->Stopping = FALSE;
    Dev->TimerStopping = FALSE;
    KeReleaseSpinLock(&Dev->TimerLock, irql);
    if (Dev->IsLite) {
        (VOID)SwFanSampleTemp(Dev, NULL);
    }
    KeAcquireSpinLock(&Dev->TimerLock, &irql);
    if (!Dev->Stopping && !Dev->TimerStopping) {
        WdfTimerStart(Dev->Timer, WDF_REL_TIMEOUT_IN_MS(500));
#ifdef SWITCH_LITE_GPU
        if (Dev->EmcTimer) WdfTimerStart(Dev->EmcTimer,WDF_REL_TIMEOUT_IN_MS(500));
#endif
    }
    KeReleaseSpinLock(&Dev->TimerLock, irql);
    return STATUS_SUCCESS;
}

VOID SwFanEvtSelfManagedIoCleanup(WDFDEVICE Device)
{
    PSWFAN_DEVICE Dev = SwFanGetDevice(Device);
    NTSTATUS Status = SwFanEvtSuspend(Device);
    /* WDF can destroy this device after a failed power callback. Never leave
       the Po callback pointing at a device context that is being freed. */
    SwFanTrace(L"CpuCleanupStatus", (ULONG)Status);
    if (Dev->Pmic != NULL) { WdfObjectDelete(Dev->Pmic); Dev->Pmic = NULL; }
    Dev->CpuConfigured = FALSE;
    if (Dev->DisplayCallback != NULL) {
        (VOID)PoUnregisterPowerSettingCallback(Dev->DisplayCallback);
        Dev->DisplayCallback = NULL;
    }
    if (Dev->BacklightPwm != NULL) {
        WdfWaitLockAcquire(Dev->BacklightLock, NULL);
        Dev->DisplayState = 1;
        (VOID)SwFanApplyBacklight(Dev);
        Dev->BacklightReady = FALSE;
        WdfWaitLockRelease(Dev->BacklightLock);
    }
    if (Dev->Sensor != NULL) {
        WdfObjectDelete(Dev->Sensor);
        Dev->Sensor = NULL;
    }
}

NTSTATUS SwFanEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Pnp;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    DECLARE_CONST_UNICODE_STRING(DeviceName, L"\\Device\\SwitchBacklight");
    DECLARE_CONST_UNICODE_STRING(LinkName, L"\\DosDevices\\SwitchBacklight");
    WDFDEVICE Device;
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Pnp);
    Pnp.EvtDevicePrepareHardware = SwFanEvtPrepareHardware;
    Pnp.EvtDeviceReleaseHardware = SwFanEvtReleaseHardware;
    Pnp.EvtDeviceD0Entry = SwFanEvtD0Entry;
    Pnp.EvtDeviceD0Exit = SwFanEvtD0Exit;
    Pnp.EvtDeviceSelfManagedIoInit = SwFanEvtSelfManagedIoInit;
    Pnp.EvtDeviceSelfManagedIoCleanup = SwFanEvtSelfManagedIoCleanup;
    Pnp.EvtDeviceSelfManagedIoSuspend = SwFanEvtSuspend;
    Pnp.EvtDeviceSelfManagedIoRestart = SwFanEvtRestart;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Pnp);
    Status = WdfDeviceInitAssignName(DeviceInit, &DeviceName);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SWFAN_DEVICE);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    KeInitializeSpinLock(&SwFanGetDevice(Device)->TimerLock);
    Status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &SwFanGetDevice(Device)->BacklightLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &SwFanGetDevice(Device)->PmicLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    Status = WdfWaitLockCreate(&Attributes, &SwFanGetDevice(Device)->SensorLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = WdfWaitLockCreate(&Attributes, &SwFanGetDevice(Device)->CpuLock);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = WdfWaitLockCreate(&Attributes, &SwFanGetDevice(Device)->FanLock);
    if (!NT_SUCCESS(Status)) return Status;
#ifdef SWITCH_LITE_GPU
    Status=WdfWaitLockCreate(&Attributes,&SwFanGetDevice(Device)->EmcLock);
    if (!NT_SUCCESS(Status)) return Status;
    WDF_WORKITEM_CONFIG work;
    WDF_WORKITEM_CONFIG_INIT(&work,SwFanEmcWorkItem);
    work.AutomaticSerialization=FALSE;
    Status=WdfWorkItemCreate(&work,&Attributes,&SwFanGetDevice(Device)->EmcWorkItem);
    if (!NT_SUCCESS(Status)) return Status;
    WDF_DPC_CONFIG dpc;
    WDF_DPC_CONFIG_INIT(&dpc,SwFanEmcDpc);
    dpc.AutomaticSerialization=FALSE;
    Status=WdfDpcCreate(&dpc,&Attributes,&SwFanGetDevice(Device)->EmcDpc);
    if (!NT_SUCCESS(Status)) return Status;
#endif
    Status = WdfDeviceCreateSymbolicLink(Device, &LinkName);
    SwFanTrace(L"SymbolicLinkStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchSequential);
    QueueConfig.EvtIoDeviceControl = SwFanEvtIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfIoQueueCreate(Device, &QueueConfig, &Attributes, WDF_NO_HANDLE);
#ifdef SWITCH_LITE_GPU
    if (NT_SUCCESS(Status)) Status = SwFanHost1xCreateQueue(Device);
#endif
    return Status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    if (RegistryPath->Length / sizeof(WCHAR) + 12 < RTL_NUMBER_OF(SwFanParamsPath)) {
        RtlCopyMemory(SwFanParamsPath, RegistryPath->Buffer, RegistryPath->Length);
        RtlCopyMemory(&SwFanParamsPath[RegistryPath->Length / sizeof(WCHAR)], L"\\Parameters", sizeof(L"\\Parameters"));
        (VOID)RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath);
    }
    SwFanTrace(L"DriverVersion", 33);
    WDF_DRIVER_CONFIG_INIT(&Config, SwFanEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
