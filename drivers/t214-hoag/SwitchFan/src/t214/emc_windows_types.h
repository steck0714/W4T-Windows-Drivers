/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SWITCHFAN_EMC_WINDOWS_TYPES_H
#define SWITCHFAN_EMC_WINDOWS_TYPES_H
#include "emc_handoff.h"
#include "emc_bpmp.h"
#include "emc_control.h"
#include "actmon.h"
#include "actmon_irq.h"
#include "emc_governor.h"
#include "emc_client_floor.h"
#include "emc_display_qos.h"
#include "emc_ptsa.h"
#include "emc_ptsa_resources.h"
#include "../emc_ioctl.h"
typedef struct {
    LITE_EMC_HANDOFF Handoff;
    volatile UCHAR *Semaphore, *Doorbell, *Mail;
    UCHAR MailResources;
    BOOLEAN ProbeAttempted, CpuFloorInitialized, FloorsInitialized, Active;
    struct sw_emc_mailbox Mailbox;
    struct sw_emc_control Control;
    SWFAN_EMC_STATUS Published;
    ULONGLONG SampleTime;
    WDFIOTARGET CoreTarget;
    WDFDEVICE Device;
    volatile UCHAR *ActmonMap[SwActmonMapCount];
    struct sw_actmon Actmon;
    struct sw_actmon_irq IrqState;
    WDFINTERRUPT Interrupt;
    BOOLEAN InterruptOnline;
    ULONGLONG DemandInterrupts;
    ULONG DemandKhz;
    volatile LONG CpuRateKhz;
    volatile ULONG *QosMap[SwQosRegisterCount];
    volatile ULONG *PtsaMap[SW_EMC_PTSA_RESOURCE_COUNT];
    struct sw_emc_qos Qos;
    ULONG TransitionCount;
    BOOLEAN ActmonProbeAttempted;
    int EnumerationError, ActmonError, QosError;
    NTSTATUS StopStatus;
} SWFAN_EMC;
#endif
