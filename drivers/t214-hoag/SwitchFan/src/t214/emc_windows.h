/* SPDX-License-Identifier: GPL-2.0-only
 * PASSIVE_LEVEL binding for the firmware-reserved threaded BPMP channel.
 * ACTMON demand drives trained BPMP rates after native GPU ownership is live.
 * Display QoS and rail votes are verified before SET_RATE; rail votes are
 * lowered only after both rate and QoS readbacks succeed.
 * Firmware boot clocks are not samples.
 */
#ifndef SWITCHFAN_EMC_WINDOWS_H
#define SWITCHFAN_EMC_WINDOWS_H
#include "../../../SwitchPowerButton/src/core_voltage.h"
/* Only the register ranges declared in actmon.h and authorized by translated
 * ACPI resources are mapped; no whole CAR/MC aperture is newly claimed. */
static uint32_t SwActmonRead(void *p, unsigned map, uint32_t offset)
{ return READ_REGISTER_ULONG((PULONG)(((SWFAN_EMC *)p)->ActmonMap[map]+offset)); }
static void SwActmonWrite(void *p, unsigned map, uint32_t offset, uint32_t value)
{
    WRITE_REGISTER_ULONG((PULONG)(((SWFAN_EMC *)p)->ActmonMap[map]+offset),value);
    KeMemoryBarrier();
}
static void SwActmonDelay(void *p, uint32_t us)
{ UNREFERENCED_PARAMETER(p); KeStallExecutionProcessor(us); }
static struct sw_actmon_io SwActmonIo(SWFAN_EMC *e)
{ return (struct sw_actmon_io){e,SwActmonRead,SwActmonWrite,SwActmonDelay}; }

enum sw_emc_irq_operation { SwEmcIrqSample, SwEmcIrqRate, SwEmcIrqMask, SwEmcIrqRestore };
typedef struct {
    enum sw_emc_irq_operation Operation;
    ULONG ActualKhz, MaximumKhz, ClockMKhz, AverageKhz, RawKhz, BoostKhz;
    ULONGLONG Interrupts;
    int Result;
} SW_EMC_IRQ_REQUEST;

/* Short DIRQL-only MMIO work, synchronized against the ISR. The passive
 * owner drops this lock before sending BPMP requests or taking rail locks. */
static BOOLEAN SwEmcSynchronize(WDFINTERRUPT Interrupt, WDFCONTEXT Context)
{
    PSWFAN_DEVICE d=SwFanGetDevice(WdfInterruptGetDevice(Interrupt));
    SWFAN_EMC *e=&d->Emc;
    SW_EMC_IRQ_REQUEST *r=Context;
    struct sw_actmon_io io=SwActmonIo(e);
    uint64_t now=KeQueryInterruptTime()/10000u;
    r->Result=SwActmonOk;
    if (r->Operation==SwEmcIrqMask) {
        sw_actmon_irq_mask(&e->IrqState,&io);
        return TRUE;
    }
    if (r->Operation==SwEmcIrqRestore) {
        bool marks=sw_actmon_irq_restore(&e->IrqState,&io);
        bool base=sw_actmon_stop(&e->Actmon,&io);
        if (!marks || !base) r->Result=SwActmonReadback;
        else RtlZeroMemory(&e->IrqState,sizeof(e->IrqState));
        return TRUE;
    }
    if (!e->InterruptOnline || d->Stopping || d->TimerStopping) {
        r->Result=SwActmonInvalid; return TRUE;
    }
    if (r->Operation==SwEmcIrqRate) {
        r->Result=sw_actmon_irq_rate(&e->IrqState,&io,r->ActualKhz); return TRUE;
    }
    if (!e->ActmonProbeAttempted) {
        e->ActmonProbeAttempted=TRUE;
        r->Result=sw_actmon_start(&e->Actmon,&io,r->ClockMKhz,r->ActualKhz,now);
        if (!r->Result) r->Result=sw_actmon_irq_start(&e->IrqState,&e->Actmon,&io,
                                                    r->ActualKhz,r->MaximumKhz);
        if (r->Result) { e->ActmonError=r->Result; return TRUE; }
    }
    if (e->ActmonError || e->IrqState.error) {
        r->Result=e->ActmonError?e->ActmonError:e->IrqState.error; return TRUE;
    }
    if (now<e->Actmon.ready_ms) { r->Result=SwActmonWaiting; return TRUE; }
    if ((SwActmonRead(e,SwActmonSource,0)&SW_ACTMON_SOURCE_MASK)!=SW_ACTMON_CLK_M ||
        (SwActmonRead(e,SwActmonResetStatus,0)&SW_ACTMON_GATE) ||
        !(SwActmonRead(e,SwActmonGateStatus,0)&SW_ACTMON_GATE)) {
        r->Result=sw_actmon_irq_fail(&e->IrqState,&io); return TRUE;
    }
    if (r->ActualKhz!=e->IrqState.current_khz)
        r->Result=sw_actmon_irq_rate(&e->IrqState,&io,r->ActualKhz);
    if (!r->Result) r->Result=sw_actmon_irq_counts(&e->IrqState,&io);
    if (!r->Result) {
        r->AverageKhz=e->IrqState.average_count/SW_ACTMON_PERIOD;
        r->RawKhz=e->IrqState.raw_count/SW_ACTMON_PERIOD;
        r->BoostKhz=e->IrqState.boost_khz;
        r->Interrupts=e->IrqState.interrupts;
    }
    return TRUE;
}

static int SwEmcIrqRequest(SWFAN_EMC *e, SW_EMC_IRQ_REQUEST *r)
{
    r->Result=SwActmonInvalid;
    if (e->Interrupt) (VOID)WdfInterruptSynchronize(e->Interrupt,SwEmcSynchronize,r);
    return r->Result;
}

static BOOLEAN SwFanEmcIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    PSWFAN_DEVICE d=SwFanGetDevice(WdfInterruptGetDevice(Interrupt));
    struct sw_actmon_io io=SwActmonIo(&d->Emc);
    UNREFERENCED_PARAMETER(MessageId);
    if (!d->Emc.InterruptOnline || !d->Emc.IrqState.running) return FALSE;
    if (!sw_actmon_irq_service(&d->Emc.IrqState,&io)) return FALSE;
    if (d->TimerStopping || d->Stopping) sw_actmon_irq_mask(&d->Emc.IrqState,&io);
    else (VOID)WdfDpcEnqueue(d->EmcDpc);
    return TRUE;
}
static NTSTATUS SwFanEmcInterruptEnable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    UNREFERENCED_PARAMETER(Interrupt);
    SwFanGetDevice(Device)->Emc.InterruptOnline=TRUE;
    return STATUS_SUCCESS;
}
static NTSTATUS SwFanEmcInterruptDisable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    SWFAN_EMC *e=&SwFanGetDevice(Device)->Emc;
    struct sw_actmon_io io=SwActmonIo(e);
    UNREFERENCED_PARAMETER(Interrupt);
    sw_actmon_irq_mask(&e->IrqState,&io);
    e->InterruptOnline=FALSE;
    return STATUS_SUCCESS;
}
static VOID SwFanEmcDpc(WDFDPC Dpc)
{
    PSWFAN_DEVICE d=SwFanGetDevice((WDFDEVICE)WdfDpcGetParentObject(Dpc));
    KIRQL irql;
    KeAcquireSpinLock(&d->TimerLock,&irql);
    if (!d->Stopping && !d->TimerStopping) WdfWorkItemEnqueue(d->EmcWorkItem);
    KeReleaseSpinLock(&d->TimerLock,irql);
}

static VOID SwFanEmcCpuRateChanged(PSWFAN_DEVICE d, ULONG Khz)
{
    KIRQL irql;
    if (!Khz || InterlockedExchange(&d->Emc.CpuRateKhz,(LONG)Khz)==(LONG)Khz) return;
    KeAcquireSpinLock(&d->TimerLock,&irql);
    if (!d->Stopping && !d->TimerStopping && d->EmcDpc) (VOID)WdfDpcEnqueue(d->EmcDpc);
    KeReleaseSpinLock(&d->TimerLock,irql);
}

/* Individual register resources, never the MC/SMMU aperture. Only the first
 * six are writable; timing-table, DRAM type and LA inputs are read-only. */
static const ULONG SwEmcQosResources[SwQosRegisterCount]={
    0x7001941cu,0x70019420u,0x70019424u,
    0x7001947cu,0x70019480u,0x70019484u,
    0x70019960u,0x7001944cu,0x7001950cu,
    0x70019428u,0x70019434u,0x70019494u,0x700194a0u,0x70019440u,
    0x700192e8u,0x70019690u,0x7001b104u
};
static uint32_t SwQosRead(void *p, enum sw_emc_qos_register r)
{ return READ_REGISTER_ULONG((PULONG)((SWFAN_EMC *)p)->QosMap[r]); }
static void SwQosWrite(void *p, enum sw_emc_qos_register r, uint32_t value)
{
    WRITE_REGISTER_ULONG((PULONG)((SWFAN_EMC *)p)->QosMap[r],value);
    KeMemoryBarrier();
}
#include "emc_ptsa_scale.h"
static int SwPtsaRead(void *p, uint32_t offset, uint32_t *value)
{
    SWFAN_EMC *e=p;
    for (ULONG i=0;i<SW_EMC_PTSA_RESOURCE_COUNT;++i) {
        if (sw_emc_ptsa_resources[i].offset!=offset) continue;
        if (!e->PtsaMap[i]) return 0;
        *value=READ_REGISTER_ULONG((PULONG)e->PtsaMap[i]);
        return *value!=MAXULONG;
    }
    return 0;
}
static int SwPtsaWrite(void *p, uint32_t offset, uint32_t value)
{
    SWFAN_EMC *e=p;
    for (ULONG i=0;i<SW_EMC_PTSA_RESOURCE_COUNT;++i) {
        if (sw_emc_ptsa_resources[i].offset!=offset) continue;
        if (!sw_emc_ptsa_resources[i].writable || !e->PtsaMap[i]) return 0;
        WRITE_REGISTER_ULONG((PULONG)e->PtsaMap[i],value);
        KeMemoryBarrier();
        return 1;
    }
    return 0;
}
static int SwPtsaBootstrap(void *p, uint32_t current, uint32_t display, uint32_t ring)
{
    SWFAN_EMC *e=p;
    struct sw_emc_ptsa_failure failure={0};
    const struct sw_emc_ptsa_io io={e,SwPtsaRead,SwPtsaWrite,&failure};
    int result;
    if (!LiteEmcHandoffValid(&e->Handoff) || !e->Control.ready ||
        e->Control.fault || !e->Control.rate_count) return SwEmcNotReady;
    for (ULONG i=0;i<SW_EMC_PTSA_RESOURCE_COUNT;++i)
        if (!e->PtsaMap[i]) return SwEmcNotReady;
    result=sw_emc_ptsa_bootstrap(&io,current,e->Control.rates[e->Control.rate_count-1u],display,ring);
    /* Temporary first-mismatch details; no extra MMIO, no periodic logging.
     * Remove with the existing prepare snapshot after native QoS validation. */
    if (result && failure.offset) {
        SwFanTrace(L"EmcPtsaMismatchOffset",failure.offset);
        SwFanTrace(L"EmcPtsaMismatchExpected",failure.expected);
        SwFanTrace(L"EmcPtsaMismatchObserved",failure.observed);
    }
    return result;
}
static int SwPtsaScale(void *p, const struct sw_emc_ptsa_clock *clock, uint32_t ring)
{
    SWFAN_EMC *e=p;
    struct sw_emc_ptsa_failure failure={0};
    const struct sw_emc_ptsa_io io={e,SwPtsaRead,SwPtsaWrite,&failure};
    if (!LiteEmcHandoffValid(&e->Handoff) || !e->Control.ready ||
        e->Control.fault || !e->Qos.prepared) return SwEmcNotReady;
    for (ULONG i=0;i<SW_EMC_PTSA_RESOURCE_COUNT;++i)
        if (!e->PtsaMap[i]) return SwEmcNotReady;
    int result=sw_emc_ptsa_scale(&io,clock,ring);
    if (result && failure.offset) {
        SwFanTrace(L"EmcPtsaMismatchOffset",failure.offset);
        SwFanTrace(L"EmcPtsaMismatchExpected",failure.expected);
        SwFanTrace(L"EmcPtsaMismatchObserved",failure.observed);
    }
    return result;
}
static int SwPtsaFinishZeroClock(void *p, const struct sw_emc_ptsa_clock *clock, uint32_t ring)
{
    SWFAN_EMC *e=p;
    struct sw_emc_ptsa_failure failure={0};
    const struct sw_emc_ptsa_io io={e,SwPtsaRead,SwPtsaWrite,&failure};
    if (!LiteEmcHandoffValid(&e->Handoff) || !e->Control.ready || e->Control.fault ||
        !e->Qos.prepared || !e->Qos.pending || e->Qos.pending_terms>=e->Qos.budget.count)
        return SwEmcNotReady;
    const struct sw_emc_qos_point *target=&e->Qos.budget.points[e->Qos.pending_terms];
    if (e->Control.actual_khz!=target->khz || e->Control.requested_khz!=target->khz ||
        clock->grant!=target->clock.grant || clock->cpu_read!=target->clock.cpu_read ||
        clock->cpu_write!=target->clock.cpu_write || ring!=target->ring) return SwEmcMismatch;
    for (ULONG i=0;i<SW_EMC_PTSA_RESOURCE_COUNT;++i)
        if (!e->PtsaMap[i]) return SwEmcNotReady;
    int result=sw_emc_ptsa_finish_zero_clock(&io,clock,ring);
    if (result && failure.offset) {
        SwFanTrace(L"EmcPtsaMismatchOffset",failure.offset);
        SwFanTrace(L"EmcPtsaMismatchExpected",failure.expected);
        SwFanTrace(L"EmcPtsaMismatchObserved",failure.observed);
    }
    return result;
}
static struct sw_emc_qos_io SwQosIo(SWFAN_EMC *e)
{ return (struct sw_emc_qos_io){e,SwQosRead,SwQosWrite,SwPtsaBootstrap,SwPtsaScale,SwPtsaFinishZeroClock}; }
static int SwEmcPrepareQos(void *p, uint32_t before, uint32_t after)
{
    PSWFAN_DEVICE d=p;
    SWFAN_EMC *e=&d->Emc;
    struct sw_emc_qos_io io=SwQosIo(e);
    uint32_t floor=sw_emc_select(&e->Control,1,
        sw_emc_runtime_floor(sw_emc_panel_bw_floor(78000,4)),0);
    if (d->Stopping || !floor || after<floor) return e->QosError=SwEmcNotReady;
    for (ULONG i=0;i<SwQosRegisterCount;++i)
        if (!e->QosMap[i]) return e->QosError=SwEmcNotReady;
    uint32_t first=0;
    while (first<e->Control.rate_count && e->Control.rates[first]<floor) ++first;
    if (first==e->Control.rate_count) return e->QosError=SwEmcNotReady;
    e->QosError=sw_emc_qos_prepare(&e->Qos,&io,e->Control.rates+first,
        e->Control.rate_count-first,before,after);
    if (e->QosError && e->QosError!=SwEmcNotReady) {
        /* One rejected prepare latches the transition fault. Record only its
         * already-read, initialized words, at PASSIVE_LEVEL without rail locks.
         * Remove this temporary snapshot once the QoS contract is verified. */
        SwFanTrace(L"EmcQosPrepareResult", (ULONG)e->QosError);
        SwFanTrace(L"EmcQosPreparePrepared", (ULONG)e->Qos.prepared);
        SwFanTrace(L"EmcQosPrepareReadError", (ULONG)e->Qos.prepare_read_error);
        if (SwFanParamsPath[0] != L'\0')
            (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath,
                L"EmcQosPrepareReadback", REG_BINARY, e->Qos.prepare_readback,
                sizeof(e->Qos.prepare_readback));
        if (e->Qos.prepared) {
            /* Latest required check, which may precede a failed scale/write;
             * do not imply it is a new post-failure MMIO sample. */
            SwFanTrace(L"EmcQosPrepareLastCheckError", (ULONG)e->Qos.verify_read_error);
            if (SwFanParamsPath[0] != L'\0')
                (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath,
                    L"EmcQosPrepareLastCheckReadback", REG_BINARY, e->Qos.verify_readback,
                    sizeof(e->Qos.verify_readback));
        }
    }
    return e->QosError;
}
static int SwEmcVerifyQos(void *p, uint32_t actual)
{
    PSWFAN_DEVICE d=p;
    struct sw_emc_qos_io io=SwQosIo(&d->Emc);
    if (d->Stopping) return d->Emc.QosError=SwEmcNotReady;
    d->Emc.QosError=sw_emc_qos_verify(&d->Emc.Qos,&io,actual);
    if (d->Emc.QosError && d->Emc.QosError!=SwEmcNotReady) {
        /* Failure is latched by the serialized controller after this return.
         * Preserve only its already-required reads, not another MMIO scan. */
        SwFanTrace(L"EmcQosVerifyResult",(ULONG)d->Emc.QosError);
        SwFanTrace(L"EmcQosVerifyReadError",(ULONG)d->Emc.Qos.verify_read_error);
        SwFanTrace(L"EmcQosVerifyKhz",actual);
        if (SwFanParamsPath[0] != L'\0')
            (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath,
                L"EmcQosVerifyReadback",REG_BINARY,d->Emc.Qos.verify_readback,
                sizeof(d->Emc.Qos.verify_readback));
    }
    return d->Emc.QosError;
}

static VOID SwFanEmcSampleLoad(PSWFAN_DEVICE Dev, SWFAN_EMC_STATUS *Status)
{
    SWFAN_EMC *e=&Dev->Emc;
    uint32_t average=0,raw=0;
    BOOLEAN enabled;
    if (e->Control.fault) {
        SW_EMC_IRQ_REQUEST mask={.Operation=SwEmcIrqMask};
        if (e->Interrupt) (VOID)SwEmcIrqRequest(e,&mask);
        Status->ActmonError=SwEmcNotReady;
        Status->QosError=e->QosError;Status->TransitionCount=e->TransitionCount;
        return;
    }
    if (!e->Control.ready || !(Status->Flags&SWFAN_EMC_RATE_VALID) || Dev->Stopping) {
        Status->ActmonError=SwEmcNotReady;
        return;
    }
    /* Legacy ACPI may expose telemetry, but cannot claim IRQ-driven scaling. */
    if (!e->Interrupt) { Status->ActmonError=SwActmonInvalid; return; }
    for (ULONG i=0;i<SwActmonMapCount;++i)
        if (!e->ActmonMap[i]) { Status->ActmonError=SwActmonInvalid; return; }
    if (!e->ActmonError) {
        SW_EMC_IRQ_REQUEST request={0};
        request.Operation=SwEmcIrqSample;
        request.ActualKhz=Status->ActualKhz;
        request.MaximumKhz=e->Control.rates[e->Control.rate_count-1u];
        request.ClockMKhz=Dev->Dfll.Handoff.ClockMKhz;
        int result=SwEmcIrqRequest(e,&request);
        Status->ActmonError=result;
        if (result==SwActmonOk) {
            average=request.AverageKhz;raw=request.RawKhz;
            Status->Flags|=SWFAN_EMC_ACTMON_VALID;
            if (request.Interrupts) Status->Flags|=SWFAN_EMC_WATERMARK_IRQ;
            Status->AverageActiveKhz=average; Status->RawActiveKhz=raw;
            Status->DisplayFloorKhz=sw_emc_panel_bw_floor(78000u,4u);
            /* Timer reads refresh telemetry; only an IRQ or the initial
             * sample changes the demand. Reading status never adds a boost. */
            if (!e->DemandKhz || request.Interrupts!=e->DemandInterrupts) {
                e->DemandKhz=sw_emc_irq_target(&e->Control,average,request.BoostKhz,Status->DisplayFloorKhz);
                e->DemandInterrupts=request.Interrupts;
            }
            ULONG cpu_khz=(ULONG)InterlockedCompareExchange(&e->CpuRateKhz,0,0);
            Status->TargetKhz=sw_emc_client_target(&e->Control,e->DemandKhz,
                cpu_khz?cpu_khz:Dev->BootCpuKhz,Status->DisplayFloorKhz);
            /* No EMC transition during firmware/BasicDisplay handoff or the
             * internal fixed recovery mode. Do not hold either CPU or PMIC
             * locks while waiting for the GPU's quiescent voltage ACK. */
            SwFanCpuLock(Dev);
            enabled=e->CpuFloorInitialized && e->FloorsInitialized &&
                    Dev->PowerMode<SwNvpCount && !Dev->Stopping && !Dev->TimerStopping &&
                    Dev->TempValid && !Dev->Dfll.FaultLatched && SwFanGpuLiveCounter(Dev);
            SwFanCpuUnlock(Dev);
            SwFanPmicLock(Dev);
            enabled=enabled && Dev->EmcGpuAbiTime &&
                KeQueryInterruptTime()-Dev->EmcGpuAbiTime<=60000000ULL;
            SwFanPmicUnlock(Dev);
            if (enabled && !e->Control.fault && Status->TargetKhz) {
                if (!e->Qos.prepared || Status->TargetKhz!=Status->ActualKhz || e->Control.last_error) {
                    uint32_t previous=Status->ActualKhz;
                    uint32_t step=sw_emc_transition_step(&e->Control,previous,Status->TargetKhz);
                    int transition;
                    if (step) transition=sw_emc_set_rate(&e->Control,step);
                    else {
                        transition=SwEmcInvalid;
                        e->Control.last_error=transition;
                        e->Control.fault=1;
                    }
                    Status->LastError=transition;
                    Status->ActualKhz=e->Control.actual_khz;
                    if (!transition && previous!=Status->ActualKhz) {
                        ++e->TransitionCount;
                        request.Operation=SwEmcIrqRate;
                        request.ActualKhz=Status->ActualKhz;
                        e->ActmonError=SwEmcIrqRequest(e,&request);
                        Status->ActmonError=e->ActmonError;
                    }
                    if (!Status->ActualKhz) Status->Flags&=~SWFAN_EMC_RATE_VALID;
                } else {
                    int check=SwEmcVerifyQos(Dev,Status->ActualKhz);
                    if (check) { e->Control.fault=1; e->Control.last_error=check; Status->LastError=check; }
                }
                if (!Status->LastError && !e->ActmonError && !e->Control.fault && e->Qos.prepared)
                    Status->Flags|=SWFAN_EMC_DYNAMIC_ENABLED|SWFAN_EMC_QOS_READY;
            }
        } else if (result<0) e->ActmonError=result;
    } else Status->ActmonError=e->ActmonError;
    Status->QosError=e->QosError;
    Status->TransitionCount=e->TransitionCount;
    if (e->Control.fault) {
        SW_EMC_IRQ_REQUEST mask={.Operation=SwEmcIrqMask};
        (VOID)SwEmcIrqRequest(e,&mask);
        Status->Flags|=SWFAN_EMC_TRANSITION_FAULT;
        Status->Flags&=~(SWFAN_EMC_DYNAMIC_ENABLED|SWFAN_EMC_WATERMARK_IRQ);
    }
}
static int SwEmcCoreFloor(PSWFAN_DEVICE Dev, ULONG Uv);

static NTSTATUS SwFanEmcStopLoad(PSWFAN_DEVICE Dev)
{
    SWFAN_EMC *e=&Dev->Emc;
    uint32_t actual=0;
    int step_err=SwEmcOk;
    if (e->Control.boot_khz) {
        step_err=e->Control.fault ? SwEmcIo : sw_emc_read_rate(&e->Control,&actual);
    } else if (LiteEmcHandoffValid(&e->Handoff)) {
        step_err=SwEmcNotReady;
    }
    if (!step_err && actual > 1600000u) {
        uint32_t safe_khz=sw_emc_select(&e->Control,1600000u,sw_emc_panel_bw_floor(78000u,4u),1600000u);
        step_err=safe_khz ? sw_emc_set_rate(&e->Control,safe_khz) : SwEmcProtocol;
        if (!step_err) actual=e->Control.actual_khz;
    }
    if (!step_err && actual && actual <= 1600000u) {
        if (e->CoreTarget) step_err=SwEmcCoreFloor(Dev,0u);
        if (!step_err) e->Control.core_floor_mv=0;
    }
    if (!step_err && actual && actual <= 1600000u) {
        NTSTATUS rail=STATUS_SUCCESS;
        SwFanPmicLock(Dev);
        if (!tg_emc_gpu_request(&Dev->EmcGpuVote,0u,TRUE)) rail=STATUS_INVALID_DEVICE_STATE;
        else if (Dev->EmcGpuVote.released) rail=SwFanGpuApplyReleased(Dev);
        SwFanPmicUnlock(Dev);
        /* Zero is the memory requirement at this confirmed clock, not a
         * fabricated GPU completion. A live GPU consumes the pending vote. */
        if (NT_SUCCESS(rail)) e->Control.gpu_floor_mv=0;
        else step_err=SwEmcIo;
    }
    SW_EMC_IRQ_REQUEST request={.Operation=SwEmcIrqRestore};
    if (e->Actmon.owned && SwEmcIrqRequest(e,&request)) return STATUS_DEVICE_HARDWARE_ERROR;
    e->ActmonProbeAttempted=FALSE;
    e->ActmonError=0;e->DemandKhz=0;e->DemandInterrupts=0;
    if (step_err==SwEmcNotReady) return STATUS_DEVICE_NOT_READY;
    return step_err ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_SUCCESS;
}
static uint32_t SwEmcToken(void *p)
{ return READ_REGISTER_ULONG((PULONG)((SWFAN_EMC *)p)->Semaphore); }
static void SwEmcClear(void *p, uint32_t value)
{ WRITE_REGISTER_ULONG((PULONG)(((SWFAN_EMC *)p)->Semaphore + 8), value); }
static void SwEmcReadMail(void *p, uint32_t offset, void *data, uint32_t size)
{ READ_REGISTER_BUFFER_UCHAR((PUCHAR)(((SWFAN_EMC *)p)->Mail + offset), data, size); }
static void SwEmcWriteMail(void *p, uint32_t offset, const void *data, uint32_t size)
{ WRITE_REGISTER_BUFFER_UCHAR((PUCHAR)(((SWFAN_EMC *)p)->Mail + offset), (PUCHAR)data, size); }
static void SwEmcBarrier(void *p)
{ UNREFERENCED_PARAMETER(p); KeMemoryBarrier(); }
static void SwEmcRing(void *p)
{ WRITE_REGISTER_ULONG((PULONG)((SWFAN_EMC *)p)->Doorbell, 1u << 6); }
static uint64_t SwEmcTime(void *p)
{ UNREFERENCED_PARAMETER(p); return KeQueryInterruptTime() / 10u; }
static void SwEmcWait(void *p)
{
    LARGE_INTEGER delay;
    UNREFERENCED_PARAMETER(p);
    delay.QuadPart = -10000; /* 1 ms, interrupts enabled, never a spin lock */
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
}
static int SwEmcMessage(void *p, uint32_t mrq, const void *input, uint32_t in_size,
                        void *output, uint32_t out_size)
{
    PSWFAN_DEVICE d = p;
    if (d->Stopping) return SwEmcNotReady;
    return sw_emc_mailbox_message(&d->Emc.Mailbox, mrq, input, in_size, output, out_size);
}

static int SwEmcCoreFloor(PSWFAN_DEVICE Dev, ULONG Uv)
{
    SWFAN_EMC *e = &Dev->Emc;
    SWCORE_VOTE vote = {SWCORE_VOTE_VERSION, 1u, Uv, 0, 0, 0};
    WDF_MEMORY_DESCRIPTOR memory;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR bytes = 0;
    NTSTATUS status;
    if (Dev->Stopping) return SwEmcNotReady;
    /* Passive EMC timer/work are serialized by EmcLock (Suspend joins both).
     * Let KMDF close/cancel/reopen on PnP notifications. No callback deletes
     * a target that this thread may still be using. Replace a permanently
     * closed target here, after our previous synchronous request has drained. */
    if (e->CoreTarget) {
        WDF_IO_TARGET_STATE state=WdfIoTargetGetState(e->CoreTarget);
        if (state==WdfIoTargetClosed || state==WdfIoTargetDeleted) {
            WdfIoTargetClose(e->CoreTarget);
            WdfObjectDelete(e->CoreTarget);
            e->CoreTarget=NULL;
        } else if (state!=WdfIoTargetStarted) return SwEmcNotReady;
    }
    if (!e->CoreTarget) {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_IO_TARGET_OPEN_PARAMS open;
        DECLARE_CONST_UNICODE_STRING(name, SWCORE_DEVICE_NAME);
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = e->Device;
        status = WdfIoTargetCreate(e->Device, &attributes, &e->CoreTarget);
        if (!NT_SUCCESS(status)) return SwEmcNotReady;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_READ | GENERIC_WRITE);
        open.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
        status = WdfIoTargetOpen(e->CoreTarget, &open);
        if (!NT_SUCCESS(status)) {
            WdfObjectDelete(e->CoreTarget); e->CoreTarget = NULL;
            return SwEmcNotReady;
        }
    }
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, &vote, sizeof(vote));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    /* The owner uses a power-managed queue. Cancellation bounds a request
     * while it is outside D0; no clock change follows a queued/failed vote. */
    options.Timeout = WDF_REL_TIMEOUT_IN_MS(500);
    status = WdfIoTargetSendIoctlSynchronously(e->CoreTarget, NULL, IOCTL_SWCORE_EMC_VOTE,
                                              &memory, &memory, &options, &bytes);
    if (status == STATUS_DEVICE_NOT_READY || status == STATUS_IO_TIMEOUT ||
        status == STATUS_CANCELLED || status == STATUS_INVALID_DEVICE_STATE ||
        status == STATUS_INVALID_DEVICE_REQUEST || status == STATUS_DELETE_PENDING ||
        status == STATUS_DEVICE_REMOVED || status == STATUS_NO_SUCH_DEVICE)
        return SwEmcNotReady;
    if (!NT_SUCCESS(status) || bytes != sizeof(vote)) return SwEmcIo;
    return vote.Version == SWCORE_VOTE_VERSION && vote.AppliedUv >= Uv ? SwEmcOk : SwEmcMismatch;
}

static int SwEmcCpuFloor(PSWFAN_DEVICE Dev, ULONG Khz)
{
    ULONG uv = Dev->Dfll.Handoff.CpuProcessId == 0u && Khz > 2666000u ? 620000u : 600000u;
    uint32_t actual = 0;
    NTSTATUS status = STATUS_SUCCESS;
    SwFanCpuLock(Dev);
    if (Dev->Stopping || !Dev->CpuConfigured) status = STATUS_DEVICE_NOT_READY;
    else {
        BOOLEAN changed = Dev->Dfll.EmcFloorUv != uv;
        Dev->Dfll.EmcFloorUv = uv;
        if (changed && Dev->Dfll.Control.active)
            status = SwFanDfllApplyProfileLocked(Dev, Dev->PowerMode);
        if (NT_SUCCESS(status) && (!SwDfllCpuVoltage(Dev, &actual) || actual < uv))
            status = STATUS_DEVICE_NOT_READY;
    }
    SwFanCpuUnlock(Dev);
    if (NT_SUCCESS(status)) return SwEmcOk;
    return status == STATUS_DEVICE_NOT_READY ? SwEmcNotReady : SwEmcIo;
}

static int SwEmcGpuFloor(PSWFAN_DEVICE Dev, ULONG Uv, BOOLEAN MemoryDownComplete)
{
    ULONGLONG start = KeQueryInterruptTime();
    ULONG word;
    SwFanPmicLock(Dev);
    if ((!Dev->EmcGpuVote.released && (!Dev->EmcGpuAbiTime ||
         start - Dev->EmcGpuAbiTime > 30000000ULL)) ||
        !tg_emc_gpu_request(&Dev->EmcGpuVote, Uv, MemoryDownComplete)) {
        SwFanPmicUnlock(Dev);
        return SwEmcNotReady;
    }
    word = Dev->EmcGpuVote.request;
    SwFanPmicUnlock(Dev);
    for (;;) {
        BOOLEAN complete;
        NTSTATUS status = STATUS_SUCCESS;
        LARGE_INTEGER delay;
        if (Dev->Stopping) return SwEmcNotReady;
        SwFanPmicLock(Dev);
        if (Dev->EmcGpuVote.failed == word) status = STATUS_DEVICE_HARDWARE_ERROR;
        else if (Dev->EmcGpuVote.released && Dev->EmcGpuVote.acknowledged != word)
            status = SwFanGpuApplyReleased(Dev);
        complete = Dev->EmcGpuVote.acknowledged == word && Dev->EmcGpuVote.guard_uv >= Uv;
        SwFanPmicUnlock(Dev);
        if (!NT_SUCCESS(status)) return SwEmcIo;
        if (complete) return SwEmcOk;
        /* Quiescent wait expiry without GPU failure is transient busy/not-ready;
         * emc_control still latches fault if SET_RATE was already submitted. */
        if (KeQueryInterruptTime() - start >= 20000000ULL) return SwEmcNotReady;
        /* Never hold CpuLock/PmicLock while the GPU worker calls this driver
         * to change the rail, recalibrate NAPLL and acknowledge the vote. */
        delay.QuadPart = -100000;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
}

static int SwEmcRaiseFloors(void *p, uint32_t core, uint32_t gpu)
{
    PSWFAN_DEVICE d = p;
    ULONG khz = d->Emc.Control.requested_khz > d->Emc.Control.actual_khz ?
        d->Emc.Control.requested_khz : d->Emc.Control.actual_khz;
    int result = SwEmcCoreFloor(d, core * 1000u);
    if (!result) result = SwEmcCpuFloor(d, khz);
    if (!result) result = SwEmcGpuFloor(d, gpu * 1000u, FALSE);
    return result;
}
static int SwEmcSettleFloors(void *p, uint32_t core, uint32_t gpu)
{
    PSWFAN_DEVICE d = p;
    int result = SwEmcCpuFloor(d, d->Emc.Control.actual_khz);
    if (!result) result = SwEmcGpuFloor(d, gpu * 1000u, TRUE);
    if (!result) result = SwEmcCoreFloor(d, core * 1000u);
    return result;
}

/* EmcLock serializes the passive timer/work item's mailbox and controller.
 * ACTMON MMIO/state additionally uses the IRQ lock, never around BPMP/PMIC.
 * SelfManagedIoSuspend masks IRQs and joins DPC/work/timer before unmapping.
 * Power-status readers consume a copy under CpuLock and never send BPMP work.
 */
static VOID SwFanEmcPoll(PSWFAN_DEVICE Dev, WDFDEVICE Device)
{
    SWFAN_EMC *e = &Dev->Emc;
    SWFAN_EMC_STATUS status;
    uint32_t khz = 0;
    int error = SwEmcNotReady;
    ULONGLONG sample = 0;
    e->Device = Device;
    RtlZeroMemory(&status, sizeof(status));
    status.Version = SWFAN_EMC_STATUS_VERSION;
    status.SampleAgeMs = ~0u;
    status.BootKhz = Dev->Config.RamActualKhz;
    if (!e->ProbeAttempted) {
        static const struct sw_emc_mail_ops mail_ops = {
            SwEmcToken, SwEmcClear, SwEmcReadMail, SwEmcWriteMail,
            SwEmcBarrier, SwEmcRing, SwEmcTime, SwEmcWait
        };
        static const struct sw_emc_ops ops = {SwEmcMessage, SwEmcRaiseFloors, SwEmcSettleFloors,
                                            SwEmcPrepareQos, SwEmcVerifyQos};
        e->ProbeAttempted = TRUE;
        if (LiteEmcHandoffValid(&e->Handoff) && e->Mail && e->Semaphore && e->Doorbell) {
            error = sw_emc_mailbox_init(&e->Mailbox, mail_ops, e, e->Handoff.Channel);
            if (!error) error = sw_emc_probe(&e->Control, ops, Dev,
                (uint32_t)sw_emc_soc_process(e->Handoff.SocSpeedo),
                (uint32_t)sw_emc_gpu_process(e->Handoff.GpuSpeedo), 1);
        }
        e->Control.last_error = error;
        e->EnumerationError = error;
        /* Save only the bounded clock reply already consumed by probe.
         * No extra mailbox request or periodic registry write. */
        SwFanTrace(L"EmcProbeResult", (ULONG)error);
        SwFanTrace(L"EmcProbeStage", e->Control.probe_stage);
        SwFanTrace(L"EmcProbeRoundRequestKhz", e->Control.round_request_khz);
        SwFanTrace(L"EmcProbeRoundStatus", (ULONG)e->Control.round_status);
        if (SwFanParamsPath[0] != L'\0') {
            /* Remove Fan22's resolved MRQ32 diagnostic, including unused
             * firmware reply bytes. B01 voltage comes from characterization. */
            (VOID)RtlDeleteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath,
                L"EmcProbeVoltageTable");
            (VOID)RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, SwFanParamsPath,
                L"EmcProbeRoundReplyHz", REG_QWORD, &e->Control.round_reply_hz,
                sizeof(e->Control.round_reply_hz));
        }
    }
    status.EnumerationError = e->EnumerationError;
    /* A supported GET_RATE remains useful if enumeration is unsupported.
     * boot_khz is set only after clock 0 has identified itself as "emc". */
    if (e->Control.boot_khz) {
        status.Flags |= SWFAN_EMC_CLOCK_IDENTIFIED;
        error = sw_emc_read_rate(&e->Control, &khz);
        if (!error) {
            sample = KeQueryInterruptTime();
            status.Flags |= SWFAN_EMC_RATE_VALID;
            status.ActualKhz = khz;
            status.SampleAgeMs = 0;
            if (!e->CpuFloorInitialized) {
                /* boot_khz is the rate observed at probe, not proof of the
                 * old firmware boot clock after a driver reload. Establish
                 * the floor from this fresh GET_RATE before any SET_RATE. */
                int floor=SwEmcCpuFloor(Dev,khz);
                if (!floor) e->CpuFloorInitialized=TRUE;
                else error=floor;
            }
            if (!e->FloorsInitialized) {
                if (khz <= 1600000u) {
                    NTSTATUS rel=STATUS_SUCCESS;
                    SwFanPmicLock(Dev);
                    (VOID)tg_emc_gpu_request(&Dev->EmcGpuVote,0u,TRUE);
                    if (Dev->EmcGpuVote.released) rel=SwFanGpuApplyReleased(Dev);
                    SwFanPmicUnlock(Dev);
                    if (NT_SUCCESS(rel)) e->FloorsInitialized=TRUE;
                    else error=SwEmcIo;
                } else if (e->Control.ready) {
                    uint32_t init_core=0,init_gpu=0;
                    int floor=sw_emc_requirements(&e->Control,khz,&init_core,&init_gpu);
                    if (!floor) floor=SwEmcCoreFloor(Dev,init_core*1000u);
                    if (!floor) {
                        e->Control.core_floor_mv=init_core;
                        floor=SwEmcGpuFloor(Dev,init_gpu*1000u,TRUE);
                        if (!floor) {
                            e->Control.gpu_floor_mv=init_gpu;
                            e->FloorsInitialized=TRUE;
                        }
                    }
                    if (floor) error=floor;
                }
            }
        }
    } else error = e->Control.last_error;
    if (e->Control.ready) {
        status.Flags |= SWFAN_EMC_RATES_ENUMERATED;
        status.RateCount = e->Control.rate_count;
        RtlCopyMemory(status.RatesKhz, e->Control.rates, sizeof(status.RatesKhz));
    }
    if (e->Mailbox.poisoned) status.Flags |= SWFAN_EMC_MAILBOX_FAULT;
    if (e->Control.fault) status.Flags |= SWFAN_EMC_TRANSITION_FAULT;
    status.LastError = e->Control.fault ? e->Control.last_error : error;
    SwFanEmcSampleLoad(Dev,&status);
    if (e->Mailbox.poisoned) status.Flags|=SWFAN_EMC_MAILBOX_FAULT;
    SwFanCpuLock(Dev);
    e->Published = status;
    e->SampleTime = sample;
    SwFanCpuUnlock(Dev);
}

static VOID SwFanEvtEmcTimer(WDFTIMER Timer)
{
    WDFDEVICE device=(WDFDEVICE)WdfTimerGetParentObject(Timer);
    PSWFAN_DEVICE d=SwFanGetDevice(device);
    KIRQL irql;
    if (d->Stopping || d->TimerStopping) return;
    WdfWaitLockAcquire(d->EmcLock,NULL);
    if (!d->Stopping && !d->TimerStopping) SwFanEmcPoll(d,device);
    WdfWaitLockRelease(d->EmcLock);
    KeAcquireSpinLock(&d->TimerLock,&irql);
    if (!d->Stopping && !d->TimerStopping) WdfTimerStart(Timer,WDF_REL_TIMEOUT_IN_MS(100));
    KeReleaseSpinLock(&d->TimerLock,irql);
}

static VOID SwFanEmcWorkItem(WDFWORKITEM WorkItem)
{
    WDFDEVICE device=(WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    PSWFAN_DEVICE d=SwFanGetDevice(device);
    WdfWaitLockAcquire(d->EmcLock,NULL);
    if (!d->Stopping && !d->TimerStopping) SwFanEmcPoll(d,device);
    WdfWaitLockRelease(d->EmcLock);
}

static VOID SwFanGetEmcStatus(PSWFAN_DEVICE Dev, SWFAN_EMC_STATUS *Out)
{
    SwFanCpuLock(Dev);
    *Out = Dev->Emc.Published;
    Out->Version = SWFAN_EMC_STATUS_VERSION;
    Out->BootKhz = Dev->Config.RamActualKhz;
    Out->SampleAgeMs = Dev->Emc.SampleTime ?
        (ULONG)((KeQueryInterruptTime() - Dev->Emc.SampleTime) / 10000u) : ~0u;
    if (Dev->Stopping || Out->SampleAgeMs > 6000u) {
        Out->Flags &= ~(SWFAN_EMC_RATE_VALID|SWFAN_EMC_ACTMON_VALID|SWFAN_EMC_DYNAMIC_ENABLED|SWFAN_EMC_WATERMARK_IRQ);
        Out->ActualKhz = 0;
        Out->AverageActiveKhz=Out->RawActiveKhz=Out->TargetKhz=0;
    }
    SwFanCpuUnlock(Dev);
}
#endif
