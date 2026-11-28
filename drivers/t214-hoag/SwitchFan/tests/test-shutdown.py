#!/usr/bin/env python3
"""Exercise SwitchFan suspend/disconnect/teardown with the real lifecycle code."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/switchfan.c').read_text()
emc_source = (root / 'src/t214/emc_windows.h').read_text()


def function(text, name):
    match = re.search(
        r'^(?:static )?(?:VOID|NTSTATUS|BOOLEAN|int) ' + name + r'\([^;]*?\)\n\{',
        text, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SWITCH_LITE_GPU 1
#define TRUE true
#define FALSE false
#define NT_SUCCESS(s) ((s) >= 0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY -4
#define STATUS_IO_TIMEOUT -9
#define STATUS_DEVICE_HARDWARE_ERROR -10
#define SWFAN_EMC_RATE_VALID 1u

typedef uint32_t ULONG;
typedef unsigned char UCHAR;
typedef uint64_t ULONGLONG;
typedef int32_t LONG;
typedef int32_t NTSTATUS;
typedef bool BOOLEAN;
typedef void VOID;
typedef void *WDFDEVICE, *WDFINTERRUPT, *WDFCONTEXT, *WDFTIMER, *WDFDPC;
typedef void *WDFWORKITEM, *WDFWAITLOCK;
typedef unsigned KIRQL;
typedef enum {
    PowerActionNone, PowerActionSleep, PowerActionHibernate,
    PowerActionShutdown, PowerActionShutdownReset, PowerActionShutdownOff
} POWER_ACTION;

enum sw_emc_irq_operation { SwEmcIrqSample, SwEmcIrqRate, SwEmcIrqMask, SwEmcIrqRestore };
enum { SwActmonOk = 0, SwActmonInvalid = 1 };
typedef struct {
    enum sw_emc_irq_operation Operation;
    ULONG ActualKhz, MaximumKhz, ClockMKhz, AverageKhz, RawKhz, BoostKhz;
    ULONGLONG Interrupts;
    int Result;
} SW_EMC_IRQ_REQUEST;
struct sw_actmon_io { int unused; };
struct sw_actmon_irq { unsigned masks; bool running; };
typedef struct { ULONG ActualKhz, Flags; } SWFAN_EMC_STATUS;
typedef struct {
    WDFINTERRUPT Interrupt;
    WDFDEVICE Device;
    volatile LONG InterruptOnline;
    BOOLEAN ProbeAttempted, CpuFloorInitialized, FloorsInitialized, Active;
    BOOLEAN ActmonOwned, ActmonMappedFlag;
    SWFAN_EMC_STATUS Published;
    ULONGLONG SampleTime;
    struct sw_actmon_irq IrqState;
    NTSTATUS StopStatus;
} SWFAN_EMC;
typedef struct {
    WDFTIMER Timer, EmcTimer;
    WDFDPC EmcDpc;
    WDFWORKITEM EmcWorkItem;
    WDFWAITLOCK EmcLock;
    volatile UCHAR *Pwm;
    unsigned TimerLock;
    volatile BOOLEAN Stopping, TimerStopping;
    BOOLEAN SuspendCompleted;
    NTSTATUS SuspendStatus;
    ULONG Duty;
    SWFAN_EMC Emc;
} SWFAN_DEVICE, *PSWFAN_DEVICE;
typedef unsigned char UCHAR;

static POWER_ACTION action;
static NTSTATUS cpu_status, emc_stop_status;
static void *underlying_interrupt;
static unsigned sync_calls, irq_run_calls, restore_calls, mask_calls;
static unsigned stop_calls, cpu_calls, fan_calls, timer_stops, dpc_cancels, work_flushes;
static unsigned terminal_error_traces;
static bool emc_locked, cpu_locked;

static PSWFAN_DEVICE SwFanGetDevice(WDFDEVICE d) { return (PSWFAN_DEVICE)d; }
static WDFDEVICE WdfInterruptGetDevice(WDFINTERRUPT interrupt) { return interrupt; }
static LONG InterlockedExchange(volatile LONG *p, LONG value)
{ LONG old = *p; *p = value; return old; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG value, LONG compare)
{ LONG old = *p; if (old == compare) *p = value; return old; }
static void KeAcquireSpinLock(unsigned *lock, KIRQL *irql) { (void)lock; *irql = 0; }
static void KeReleaseSpinLock(unsigned *lock, KIRQL irql) { (void)lock; (void)irql; }
static struct sw_actmon_io SwActmonIo(SWFAN_EMC *e)
{ (void)e; return (struct sw_actmon_io){0}; }
static BOOLEAN SwActmonMapped(const SWFAN_EMC *e) { return e->ActmonMappedFlag; }
static void sw_actmon_irq_mask(struct sw_actmon_irq *irq, const struct sw_actmon_io *io)
{ (void)io; ++mask_calls; ++irq->masks; irq->running = false; }
static VOID SwEmcIrqRun(PSWFAN_DEVICE d, SW_EMC_IRQ_REQUEST *r)
{
    struct sw_actmon_io io = SwActmonIo(&d->Emc);
    ++irq_run_calls;
    if (r->Operation == SwEmcIrqMask) {
        if (!SwActmonMapped(&d->Emc)) { r->Result = SwActmonOk; return; }
        sw_actmon_irq_mask(&d->Emc.IrqState, &io);
    } else if (r->Operation == SwEmcIrqRestore) {
        if (!SwActmonMapped(&d->Emc)) { r->Result = SwActmonInvalid; return; }
        ++restore_calls;
    } else assert(!"runtime request after test disconnect");
    r->Result = SwActmonOk;
}
static BOOLEAN SwEmcSynchronize(WDFINTERRUPT interrupt, WDFCONTEXT context)
{
    SwEmcIrqRun(SwFanGetDevice(WdfInterruptGetDevice(interrupt)), context);
    return TRUE;
}
static BOOLEAN WdfInterruptSynchronize(WDFINTERRUPT interrupt,
    BOOLEAN (*callback)(WDFINTERRUPT, WDFCONTEXT), WDFCONTEXT context)
{
    ++sync_calls;
    /* Models FxInterrupt::Disconnect clearing m_Interrupt. */
    assert(underlying_interrupt != NULL);
    return callback(interrupt, context);
}
static void WdfTimerStop(WDFTIMER timer, BOOLEAN wait)
{ assert(timer && wait); ++timer_stops; }
static BOOLEAN WdfDpcCancel(WDFDPC dpc, BOOLEAN wait)
{ assert(dpc && wait); ++dpc_cancels; return TRUE; }
static void WdfWorkItemFlush(WDFWORKITEM work) { assert(work); ++work_flushes; }
static void WdfWaitLockAcquire(WDFWAITLOCK lock, void *timeout)
{ (void)timeout; assert(lock && !emc_locked); emc_locked = true; }
static void WdfWaitLockRelease(WDFWAITLOCK lock)
{ assert(lock && emc_locked); emc_locked = false; }
static void SwFanCpuLock(PSWFAN_DEVICE d) { (void)d; assert(!cpu_locked); cpu_locked = true; }
static void SwFanCpuUnlock(PSWFAN_DEVICE d) { (void)d; assert(cpu_locked); cpu_locked = false; }
static int SwEmcIrqRequest(SWFAN_EMC *e, SW_EMC_IRQ_REQUEST *r);
static NTSTATUS SwFanEmcStopLoad(PSWFAN_DEVICE d)
{
    SW_EMC_IRQ_REQUEST restore = {.Operation = SwEmcIrqRestore};
    ++stop_calls;
    if (d->Emc.ActmonOwned && SwEmcIrqRequest(&d->Emc, &restore))
        return STATUS_DEVICE_HARDWARE_ERROR;
    return emc_stop_status;
}
static NTSTATUS SwCpuApply(PSWFAN_DEVICE d, BOOLEAN overclock)
{ (void)d; assert(!overclock); ++cpu_calls; return cpu_status; }
static VOID SwFanSetDuty(PSWFAN_DEVICE d, ULONG duty)
{ ++fan_calls; d->Duty = duty; assert(duty == 76); }
static POWER_ACTION WdfDeviceGetSystemPowerAction(WDFDEVICE d)
{ (void)d; return action; }
static VOID SwFanTrace(const void *name, ULONG value)
{ (void)name; (void)value; ++terminal_error_traces; }
'''

tests = r'''
static void init(PSWFAN_DEVICE d)
{
    memset(d, 0, sizeof(*d));
    action = PowerActionNone;
    cpu_status = emc_stop_status = STATUS_SUCCESS;
    sync_calls = irq_run_calls = restore_calls = mask_calls = 0;
    stop_calls = cpu_calls = fan_calls = timer_stops = dpc_cancels = work_flushes = 0;
    terminal_error_traces = 0;
    emc_locked = cpu_locked = false;
    d->Timer = d->EmcTimer = (WDFTIMER)1;
    d->EmcDpc = (WDFDPC)1;
    d->EmcWorkItem = (WDFWORKITEM)1;
    d->EmcLock = (WDFWAITLOCK)1;
    d->Pwm = (UCHAR *)1;
    d->Emc.Interrupt = d;
    d->Emc.Device = d;
    d->Emc.Active = TRUE;
    d->Emc.ActmonOwned = TRUE;
    d->Emc.ActmonMappedFlag = TRUE;
    d->Emc.Published.Flags = SWFAN_EMC_RATE_VALID;
    underlying_interrupt = d;
    assert(SwFanEmcInterruptEnable(d, d) == STATUS_SUCCESS);
}

static void disconnect(PSWFAN_DEVICE d)
{
    assert(SwFanEmcInterruptDisable(d, d) == STATUS_SUCCESS);
    underlying_interrupt = NULL; /* simulated PKINTERRUPT = NULL */
    assert(d->Emc.InterruptOnline == 0);
}

static void test_successful_suspend_is_idempotent(void)
{
    SWFAN_DEVICE d;
    init(&d);
    assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
    assert(sync_calls == 2 && irq_run_calls == 2 && restore_calls == 1);
    assert(stop_calls == 1 && cpu_calls == 1 && fan_calls == 1);
    assert(d.SuspendCompleted && !d.Emc.Active && d.Stopping && d.TimerStopping);
    assert(!(d.Emc.Published.Flags & SWFAN_EMC_RATE_VALID));
    disconnect(&d);
    /* SelfManagedIoCleanup and ReleaseHardware both enter the callback. */
    assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
    assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
    assert(sync_calls == 2 && irq_run_calls == 2 && stop_calls == 1 && cpu_calls == 1);
}

static void test_failed_suspend_has_offline_teardown(void)
{
    SWFAN_DEVICE d;
    init(&d);
    action = PowerActionSleep;
    emc_stop_status = STATUS_DEVICE_NOT_READY;
    assert(SwFanEvtSuspend(&d) == STATUS_DEVICE_NOT_READY);
    assert(!d.SuspendCompleted && d.Emc.Active && sync_calls == 2);
    disconnect(&d);
    assert(SwFanEvtSuspend(&d) == STATUS_DEVICE_NOT_READY);
    assert(SwFanEvtSuspend(&d) == STATUS_DEVICE_NOT_READY);
    /* Both retries performed local mask/restore with zero new IRQ sync calls. */
    assert(sync_calls == 2 && irq_run_calls == 6 && restore_calls == 3);
    assert(stop_calls == 3 && !emc_locked && !cpu_locked);
}

static void test_terminal_peer_failures_are_tolerated(void)
{
    static const POWER_ACTION actions[] = {
        PowerActionShutdown, PowerActionShutdownReset, PowerActionShutdownOff
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        SWFAN_DEVICE d;
        init(&d);
        action = actions[i];
        cpu_status = STATUS_IO_TIMEOUT;       /* PMIC SPB sibling already D3 */
        emc_stop_status = STATUS_DEVICE_NOT_READY; /* CoreTarget already D3 */
        assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
        assert(d.SuspendCompleted && !d.Emc.Active && d.Stopping && d.TimerStopping);
        assert(stop_calls == 1 && cpu_calls == 1 && fan_calls == 1);
        assert(terminal_error_traces == 2);
        disconnect(&d);
        assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
        assert(SwFanEvtSuspend(&d) == STATUS_SUCCESS);
        assert(sync_calls == 2 && stop_calls == 1 && cpu_calls == 1);
    }
}

static void test_recoverable_power_errors_are_preserved(void)
{
    static const POWER_ACTION actions[] = {
        PowerActionNone, PowerActionSleep, PowerActionHibernate
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        SWFAN_DEVICE d;
        init(&d);
        action = actions[i];
        emc_stop_status = STATUS_DEVICE_NOT_READY;
        assert(SwFanEvtSuspend(&d) == STATUS_DEVICE_NOT_READY);
        assert(!d.SuspendCompleted && d.Emc.Active && terminal_error_traces == 0);
    }
}

int main(void)
{
    assert(!SwFanIsTerminalPowerAction(PowerActionNone));
    assert(!SwFanIsTerminalPowerAction(PowerActionSleep));
    assert(!SwFanIsTerminalPowerAction(PowerActionHibernate));
    assert(SwFanIsTerminalPowerAction(PowerActionShutdown));
    assert(SwFanIsTerminalPowerAction(PowerActionShutdownReset));
    assert(SwFanIsTerminalPowerAction(PowerActionShutdownOff));
    test_successful_suspend_is_idempotent();
    test_failed_suspend_has_offline_teardown();
    test_terminal_peer_failures_are_tolerated();
    test_recoverable_power_errors_are_preserved();
    {
        /* Unmapped Actmon must not touch MMIO in InterruptDisable. */
        SWFAN_DEVICE d;
        init(&d);
        d.Emc.ActmonMappedFlag = FALSE;
        disconnect(&d);
        assert(mask_calls == 0 && sync_calls == 0);
    }
    puts("PASS: SwitchFan suspend idempotence, post-disconnect MMIO cleanup, shutdown tolerance and sleep errors");
    return 0;
}
'''

parts = [
    prefix,
    function(emc_source, 'SwEmcIrqRequest'),
    function(emc_source, 'SwFanEmcInterruptEnable'),
    function(emc_source, 'SwFanEmcInterruptDisable'),
    function(source, 'SwFanIsTerminalPowerAction'),
    function(source, 'SwFanEvtSuspend'),
    tests,
]
with tempfile.TemporaryDirectory(prefix='switchfan-shutdown-test-') as tmp:
    c = Path(tmp) / 'shutdown.c'
    exe = Path(tmp) / 'shutdown'
    c.write_text('\n'.join(parts))
    subprocess.run([
        os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(c), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
