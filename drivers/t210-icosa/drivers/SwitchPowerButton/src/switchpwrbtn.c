/* W4T-CREDIT-BEGIN
 * Origin : hayato41200/Windows-for-Switch-Drivers, Erista (t210/Icosa) driver set, which matched an older
 *          ruje0504-2/Windows-for-Switch-Drivers commit (docs/HAYATO-DRIVER-AUDIT-ja.md); later edits are in CREDITS.md.
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : CTCaer/hekate (GPL-2.0): MAX77620 registers.
 * Credit block added by W4T (2026-11-18); full table in CREDITS.md.
 * W4T-CREDIT-END */
/*
 * Nintendo Switch power button driver, KMDF + Virtual HID Framework (VHF).
 *
 * ACPI device PWRB (ACPI\SWOA0601):
 *   I2cSerialBus 0x3C on I2C5: MAX77620 PMIC. The power button drives its
 *   EN0 input; there is no GPIO for it on the SoC.
 * Every 50 ms the driver reads (Hekate bdk/power/max77620.h):
 *   ONOFFSTAT 0x15 bit 2 (EN0): 1 while the button is held.
 *   ONOFFIRQ  0x0B (clear on read): bit 3 EN0 rising, bit 2 EN0 falling,
 *     so a press shorter than one poll is still seen.
 * Changes are reported as a HID System Control "System Power Down" button;
 * Windows then applies the power button action from Power Options.
 * Holding the button for several seconds still powers the PMIC off by itself.
 *
 * 1.0.0.2 (ported from windows-switch's swhid): optional hold action, off by default.
 * Registry DWORD Services\switchpwrbtn\Parameters\HoldSasMs (0 = off, otherwise 1000..6000):
 *   a press shorter than that is reported on release as before (press + release);
 *   a press held that long sends Ctrl+Alt+Del from a keyboard collection (so Windows shows its
 *   security screen with the power menu) and sends no power action for that press.
 * Edge logic lives in hold_sas.h (host test: tests/test_hold_sas.c). With HoldSasMs = 0 the
 * keyboard collection is not created and behaviour is exactly 1.0.0.1.
 */
#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <spb.h>
#include <vhf.h>
#include "hold_sas.h"

#define MAX77620_ONOFFIRQ    0x0B
#define MAX77620_ONOFFSTAT   0x15
#define ONOFFSTAT_EN0        0x04
#define ONOFFIRQ_EN0_F       0x04
#define ONOFFIRQ_EN0_R       0x08
#define POLL_MS              50
#define REPORT_ID_SYSTEM     1
#define REPORT_ID_KEYS       2
#define KEY_REPORT_SIZE      9      /* id, modifiers, reserved, 6 keys */
#define HOLD_SAS_MIN_MS      1000
#define HOLD_SAS_MAX_MS      6000   /* Windows starts its forced shutdown at a 7 s hold (Microsoft, Old New Thing "chassis buttons") */
#define HID_MOD_LCTRL        0x01
#define HID_MOD_LALT         0x04
#define HID_KEY_DELETE       0x4C

/* System Control: bit 0 System Power Down (0x81), 7 bits padding. */
#define SWPWR_SYSTEM_COLLECTION \
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, \
    0x85, REPORT_ID_SYSTEM, \
    0x09, 0x81, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, \
    0x95, 0x07, 0x81, 0x01, \
    0xC0

static UCHAR SwPwrReportDescriptor[] = {
    SWPWR_SYSTEM_COLLECTION
};

/* Same, plus a boot-style keyboard (report id 2: modifiers, reserved, 6 keys) used only for
 * Ctrl+Alt+Del when HoldSasMs is set. */
static UCHAR SwPwrReportDescriptorKbd[] = {
    SWPWR_SYSTEM_COLLECTION,
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, REPORT_ID_KEYS,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0
};

typedef struct _SWPWR_DEVICE {
    LARGE_INTEGER I2cId;
    WDFIOTARGET I2c;
    WDFTIMER Timer;
    VHFHANDLE Vhf;
    volatile BOOLEAN Stopping;
    struct swpwr_hold Hold;
    ULONG HoldSasMs;
    UCHAR Report[2];
    UCHAR KeyReport[KEY_REPORT_SIZE];
    ULONG Failures;
    LONG Polls;
    LONG Presses;
    LONG Reports;
    LONG SasReports;
} SWPWR_DEVICE, *PSWPWR_DEVICE;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SWPWR_DEVICE, SwPwrGetDevice)

typedef struct _SWPWR_TIMER {
    WDFDEVICE Device;
} SWPWR_TIMER, *PSWPWR_TIMER;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SWPWR_TIMER, SwPwrGetTimer)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SwPwrEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SwPwrEvtPrepareHardware;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT SwPwrEvtSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP SwPwrEvtSelfManagedIoCleanup;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND SwPwrEvtSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART SwPwrEvtSelfManagedIoRestart;
EVT_WDF_TIMER SwPwrEvtTimer;

static WCHAR SwPwrParamsPath[256];

static VOID SwPwrTrace(PCWSTR Name, ULONG Value)
{
    if (SwPwrParamsPath[0] != L'\0' && KeGetCurrentIrql() == PASSIVE_LEVEL) {
        (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwPwrParamsPath, Name, REG_DWORD, &Value, sizeof(Value));
    }
}

/* ------------------------------------------------------------------- I2C */

static NTSTATUS SwPwrOpenTarget(WDFDEVICE Device, LARGE_INTEGER Id, WDFIOTARGET *Target)
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

static NTSTATUS SwPwrReadReg(WDFIOTARGET Target, UCHAR Register, PUCHAR Value)
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
    Options.Timeout = WDF_REL_TIMEOUT_IN_MS(200);
    Status = WdfIoTargetSendIoctlSynchronously(Target, NULL, IOCTL_SPB_EXECUTE_SEQUENCE, &Input, NULL, &Options, &Bytes);
    if (NT_SUCCESS(Status) && Bytes != 2) {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    return Status;
}

/* ---------------------------------------------------------------- button */

static VOID SwPwrReport(PSWPWR_DEVICE Dev, BOOLEAN Pressed)
{
    HID_XFER_PACKET Packet;
    NTSTATUS Status;

    Dev->Report[0] = REPORT_ID_SYSTEM;
    Dev->Report[1] = Pressed ? 0x01 : 0x00;
    Packet.reportBuffer = Dev->Report;
    Packet.reportBufferLen = sizeof(Dev->Report);
    Packet.reportId = REPORT_ID_SYSTEM;
    Status = VhfReadReportSubmit(Dev->Vhf, &Packet);
    if (NT_SUCCESS(Status)) {
        SwPwrTrace(L"Reports", (ULONG)InterlockedIncrement(&Dev->Reports));
    } else {
        SwPwrTrace(L"LastReportError", (ULONG)Status);
    }
}

static VOID SwPwrReportKeys(PSWPWR_DEVICE Dev, BOOLEAN Down)
{
    HID_XFER_PACKET Packet;
    NTSTATUS Status;

    RtlZeroMemory(Dev->KeyReport, sizeof(Dev->KeyReport));
    Dev->KeyReport[0] = REPORT_ID_KEYS;
    if (Down) {
        Dev->KeyReport[1] = HID_MOD_LCTRL | HID_MOD_LALT;
        Dev->KeyReport[3] = HID_KEY_DELETE;
    }
    Packet.reportBuffer = Dev->KeyReport;
    Packet.reportBufferLen = sizeof(Dev->KeyReport);
    Packet.reportId = REPORT_ID_KEYS;
    Status = VhfReadReportSubmit(Dev->Vhf, &Packet);
    if (NT_SUCCESS(Status)) {
        SwPwrTrace(L"SasReports", (ULONG)InterlockedIncrement(&Dev->SasReports));
    } else {
        SwPwrTrace(L"LastSasError", (ULONG)Status);
    }
}

static VOID SwPwrApply(PSWPWR_DEVICE Dev, enum swpwr_action Action)
{
    switch (Action) {
    case SWPWR_ACT_POWER_DOWN:
        SwPwrTrace(L"Presses", (ULONG)InterlockedIncrement(&Dev->Presses));
        SwPwrReport(Dev, TRUE);
        break;
    case SWPWR_ACT_POWER_UP:
        SwPwrReport(Dev, FALSE);
        break;
    case SWPWR_ACT_SAS_DOWN:
        SwPwrReportKeys(Dev, TRUE);
        break;
    case SWPWR_ACT_SAS_UP:
        SwPwrReportKeys(Dev, FALSE);
        break;
    default:
        break;
    }
}

static ULONG SwPwrReadHoldSasMs(VOID)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];
    ULONG Value = 0, Zero = 0;

    if (SwPwrParamsPath[0] == L'\0') {
        return 0;
    }
    RtlZeroMemory(Table, sizeof(Table));
    /* DIRECT must be paired with TYPECHECK (RtlQueryRegistryValues docs); the table is on the stack
     * (nonpaged) and ends with the zeroed entry. */
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = L"HoldSasMs";
    Table[0].EntryContext = &Value;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD;
    Table[0].DefaultData = &Zero;
    Table[0].DefaultLength = sizeof(Zero);
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, SwPwrParamsPath, Table, NULL, NULL))) {
        return 0;
    }
    if (Value == 0) {
        return 0;
    }
    if (Value < HOLD_SAS_MIN_MS) {
        Value = HOLD_SAS_MIN_MS;
    }
    if (Value > HOLD_SAS_MAX_MS) {
        Value = HOLD_SAS_MAX_MS;
    }
    return Value;
}

VOID SwPwrEvtTimer(WDFTIMER Timer)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(SwPwrGetTimer(Timer)->Device);
    UCHAR Irq = 0, Stat = 0;
    BOOLEAN Held;
    enum swpwr_action Acts[2];
    int Count, Index;
    NTSTATUS Status;

    Status = SwPwrReadReg(Dev->I2c, MAX77620_ONOFFIRQ, &Irq);
    if (NT_SUCCESS(Status)) {
        Status = SwPwrReadReg(Dev->I2c, MAX77620_ONOFFSTAT, &Stat);
    }
    if (NT_SUCCESS(Status)) {
        Dev->Failures = 0;
        Held = (Stat & ONOFFSTAT_EN0) != 0;
        Count = swpwr_hold_step(&Dev->Hold, Dev->HoldSasMs, POLL_MS, Held,
                                !Held && (Irq & ONOFFIRQ_EN0_R) != 0, Acts);
        for (Index = 0; Index < Count; ++Index) {
            SwPwrApply(Dev, Acts[Index]);
        }
        if (Irq != 0) {
            SwPwrTrace(L"LastOnOffIrq", Irq);
        }
    } else if (++Dev->Failures <= 3 || (Dev->Failures % 100) == 0) {
        SwPwrTrace(L"LastI2cError", (ULONG)Status);
        SwPwrTrace(L"Failures", Dev->Failures);
    }
    if ((InterlockedIncrement(&Dev->Polls) % 200) == 0) {
        SwPwrTrace(L"Polls", (ULONG)Dev->Polls);
        SwPwrTrace(L"LastOnOffStat", Stat);
    }
    if (!Dev->Stopping) {
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));    /* one-shot passive timer, re-armed */
        if (Dev->Stopping) {
            /* Suspend/Cleanup set Stopping between the check above and this re-arm: cancel it again.
               (Wait must be FALSE here: we are inside the timer callback.) */
            (VOID)WdfTimerStop(Timer, FALSE);
        }
    }
}

/* ------------------------------------------------------------------- PnP */

NTSTATUS SwPwrEvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(Device);
    ULONG Count = WdfCmResourceListGetCount(Translated), Index;
    UNREFERENCED_PARAMETER(Raw);

    for (Index = 0; Index < Count; ++Index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = WdfCmResourceListGetDescriptor(Translated, Index);
        if (Desc != NULL && Desc->Type == CmResourceTypeConnection &&
            Desc->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            Desc->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C) {
            Dev->I2cId.LowPart = Desc->u.Connection.IdLowPart;
            Dev->I2cId.HighPart = (LONG)Desc->u.Connection.IdHighPart;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS SwPwrEvtSelfManagedIoInit(WDFDEVICE Device)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(Device);
    VHF_CONFIG Config;
    UCHAR Stat = 0, Irq = 0;
    NTSTATUS Status;

    if (Dev->I2c == NULL) {
        Status = SwPwrOpenTarget(Device, Dev->I2cId, &Dev->I2c);
        SwPwrTrace(L"OpenI2cStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    Status = SwPwrReadReg(Dev->I2c, MAX77620_ONOFFSTAT, &Stat);
    SwPwrTrace(L"FirstReadStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    (VOID)SwPwrReadReg(Dev->I2c, MAX77620_ONOFFIRQ, &Irq);    /* drop edges latched before Windows */
    SwPwrTrace(L"OnOffStatAtStart", Stat);
    SwPwrTrace(L"OnOffIrqAtStart", Irq);
    Dev->HoldSasMs = SwPwrReadHoldSasMs();
    SwPwrTrace(L"HoldSasMs", Dev->HoldSasMs);
    swpwr_hold_init(&Dev->Hold, (Stat & ONOFFSTAT_EN0) != 0);

    if (Dev->Vhf == NULL) {
        if (Dev->HoldSasMs != 0) {
            VHF_CONFIG_INIT(&Config, WdfDeviceWdmGetDeviceObject(Device), sizeof(SwPwrReportDescriptorKbd),
                            SwPwrReportDescriptorKbd);
        } else {
            VHF_CONFIG_INIT(&Config, WdfDeviceWdmGetDeviceObject(Device), sizeof(SwPwrReportDescriptor),
                            SwPwrReportDescriptor);
        }
        Config.VhfClientContext = Dev;
        Config.VendorID = 0x057E;       /* Nintendo */
        Config.ProductID = 0x5601;
        Config.VersionNumber = 0x0001;
        Status = VhfCreate(&Config, &Dev->Vhf);
        SwPwrTrace(L"VhfCreateStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            Dev->Vhf = NULL;
            return Status;
        }
        Status = VhfStart(Dev->Vhf);
        SwPwrTrace(L"VhfStartStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            VhfDelete(Dev->Vhf, TRUE);
            Dev->Vhf = NULL;
            return Status;
        }
    }
    if (Dev->Timer == NULL) {
        WDF_TIMER_CONFIG TimerConfig;
        WDF_OBJECT_ATTRIBUTES Attributes;
        WDF_TIMER_CONFIG_INIT(&TimerConfig, SwPwrEvtTimer);
        TimerConfig.AutomaticSerialization = FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SWPWR_TIMER);
        Attributes.ParentObject = Device;
        Attributes.ExecutionLevel = WdfExecutionLevelPassive;
        Status = WdfTimerCreate(&TimerConfig, &Attributes, &Dev->Timer);
        SwPwrTrace(L"TimerCreateStatus", (ULONG)Status);
        if (!NT_SUCCESS(Status)) {
            Dev->Timer = NULL;
            return Status;
        }
        SwPwrGetTimer(Dev->Timer)->Device = Device;
    }
    Dev->Stopping = FALSE;
    WdfTimerStart(Dev->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
    return STATUS_SUCCESS;
}

/* System sleep (Dx): stop polling. The press that WAKES the system (and any edge the PMIC latched while we were
 * not polling) must not be reported as a new button press, or Windows would go straight back to sleep: Restart
 * seeds the hold logic from the current level and drops the latched edges, exactly like SelfManagedIoInit. */
NTSTATUS SwPwrEvtSelfManagedIoSuspend(WDFDEVICE Device)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(Device);
    Dev->Stopping = TRUE;
    if (Dev->Timer != NULL) {
        WdfTimerStop(Dev->Timer, TRUE);
    }
    return STATUS_SUCCESS;
}

NTSTATUS SwPwrEvtSelfManagedIoRestart(WDFDEVICE Device)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(Device);
    UCHAR Stat = 0, Irq = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (Dev->I2c != NULL) {
        Status = SwPwrReadReg(Dev->I2c, MAX77620_ONOFFSTAT, &Stat);
        (VOID)SwPwrReadReg(Dev->I2c, MAX77620_ONOFFIRQ, &Irq);    /* drop edges latched while asleep */
    }
    /* If the level cannot be read, assume "held": the hold logic then waits for a release before it reports. */
    swpwr_hold_init(&Dev->Hold, NT_SUCCESS(Status) ? ((Stat & ONOFFSTAT_EN0) != 0) : 1);
    Dev->Failures = 0;
    Dev->Stopping = FALSE;
    if (Dev->Timer != NULL) {
        WdfTimerStart(Dev->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
    }
    return STATUS_SUCCESS;
}

VOID SwPwrEvtSelfManagedIoCleanup(WDFDEVICE Device)
{
    PSWPWR_DEVICE Dev = SwPwrGetDevice(Device);
    Dev->Stopping = TRUE;
    if (Dev->Timer != NULL) {
        WdfTimerStop(Dev->Timer, TRUE);
    }
    if (Dev->Vhf != NULL) {
        VhfDelete(Dev->Vhf, TRUE);
        Dev->Vhf = NULL;
    }
    if (Dev->I2c != NULL) {
        WdfIoTargetClose(Dev->I2c);
        WdfObjectDelete(Dev->I2c);
        Dev->I2c = NULL;
    }
}

NTSTATUS SwPwrEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Pnp;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE Device;
    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Pnp);
    Pnp.EvtDevicePrepareHardware = SwPwrEvtPrepareHardware;
    Pnp.EvtDeviceSelfManagedIoInit = SwPwrEvtSelfManagedIoInit;
    Pnp.EvtDeviceSelfManagedIoCleanup = SwPwrEvtSelfManagedIoCleanup;
    Pnp.EvtDeviceSelfManagedIoSuspend = SwPwrEvtSelfManagedIoSuspend;
    Pnp.EvtDeviceSelfManagedIoRestart = SwPwrEvtSelfManagedIoRestart;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Pnp);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SWPWR_DEVICE);
    return WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    if (RegistryPath->Length / sizeof(WCHAR) + 12 < RTL_NUMBER_OF(SwPwrParamsPath)) {
        RtlCopyMemory(SwPwrParamsPath, RegistryPath->Buffer, RegistryPath->Length);
        RtlCopyMemory(&SwPwrParamsPath[RegistryPath->Length / sizeof(WCHAR)], L"\\Parameters", sizeof(L"\\Parameters"));
        (VOID)RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, SwPwrParamsPath);
    }
    SwPwrTrace(L"DriverVersion", 3);
    WDF_DRIVER_CONFIG_INIT(&Config, SwPwrEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
