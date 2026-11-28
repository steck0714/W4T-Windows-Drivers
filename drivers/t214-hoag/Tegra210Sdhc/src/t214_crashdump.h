/* SPDX-License-Identifier: GPL-2.0-only
 * T214 SDPORT dump support. Uses the Hekate/Switchroot board primitives in
 * t214_sdmmc.h. Identity polling derives from this project's f6ee89ede53e
 * S4 experiment; this path allocates no DMA buffer and does not enable S4.
 */

#define NV_T214_DUMP_DRIVER_VER 0x0005010bu
#define NV_T214_DUMP_READY_BIT  (1ULL << 32)

/* Interrupt time is monotonic in 100 ns units and available at HIGH_LEVEL. */
#define T214_DUMP_COMMAND_WAIT_100NS (2ULL * 10000000)
#define T214_DUMP_DATA_WAIT_100NS    (10ULL * 10000000)
#define T214_DUMP_WRITE_ATTEMPTS 3

static VOID T214ArmDumpWait(PSDHC_EXTENSION s, ULONG required)
{
    ULONGLONG counter;
    ULONGLONG interval = (required & SDHC_IS_CMD_COMPLETE) ?
        T214_DUMP_COMMAND_WAIT_100NS : T214_DUMP_DATA_WAIT_100NS;
    s->T214DumpDeadline = KeQueryInterruptTimePrecise(&counter) + interval;
}

static USHORT T214DumpTimeoutError(PSDHC_EXTENSION s, PSDPORT_REQUEST request, ULONG events)
{
    ULONG required = request->RequiredEvents;
    ULONG arrived = events & required;
    ULONG buffers = SDHC_IS_BUFFER_READ_READY | SDHC_IS_BUFFER_WRITE_READY;
    ULONGLONG counter;

    if (!required || !s->T214DumpDeadline) return 0;
    if (arrived && ((arrived & ~buffers) || request->Type != SdRequestTypeStartTransfer ||
                    !request->Command.BlockCount)) return 0;
    if (KeQueryInterruptTimePrecise(&counter) < s->T214DumpDeadline) return 0;
    if (arrived) {
        /* A latched buffer-ready edge can outlive the block it described. */
        ULONG ready = (arrived & SDHC_IS_BUFFER_READ_READY) ?
            SDHC_PS_BUFFER_READ_ENABLE : SDHC_PS_BUFFER_WRITE_ENABLE;
        if (SdhcReadRegisterUlong(s, SDHC_PRESENT_STATE) & ready) return 0;
    }
    return (required & SDHC_IS_CMD_COMPLETE) ? SDHC_ES_CMD_TIMEOUT : SDHC_ES_DATA_TIMEOUT;
}

#ifdef _MSC_VER
#define NV_DUMP_EXPORT __declspec(dllexport)
#else
#define NV_DUMP_EXPORT
#endif

typedef struct _NV_DUMP_PREP_RECEIPT {
    ULONG Version;
    ULONG DriverVer;
    ULONG Action;
    ULONG Seq;
    ULONG Irql;
    NTSTATUS Status;
    ULONG Refs;
    ULONG Ready;
    ULONGLONG SystemTime;
    ULONGLONG PerfCounter;
    ULONGLONG PerfFreq;
    ULONG_PTR Routine;
} NV_DUMP_PREP_RECEIPT;

#ifdef _NTDDK_
C_ASSERT(sizeof(NV_DUMP_PREP_RECEIPT) == 64);
#endif

static volatile LONG g_DumpPrepSeq;
static volatile LONG64 g_DumpPrepState;
static KSPIN_LOCK g_DumpPrepLock;
static PUCHAR g_DumpMaps[5];

NV_DUMP_EXPORT NTSTATUS DumpPreInitialize(VOID);
NV_DUMP_EXPORT VOID DumpUninitialize(VOID);

static VOID T214UnmapDumpPrepared(VOID)
{
    static const ULONG lengths[5] = {0x1000, 0x4000, 0x1000, 0x1000, 0x1000};
    for (LONG i = 4; i >= 0; --i) {
        if (g_DumpMaps[i] != NULL) {
            MmUnmapIoSpace(g_DumpMaps[i], lengths[i]);
            g_DumpMaps[i] = NULL;
        }
    }
}

static VOID T214RecordDumpPrepReceipt(ULONG Action, NTSTATUS Status, ULONG Seq)
{
    LONG64 state = InterlockedCompareExchange64(&g_DumpPrepState, 0, 0);
    NV_DUMP_PREP_RECEIPT receipt;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = 1;
    receipt.DriverVer = NV_T214_DUMP_DRIVER_VER;
    receipt.Action = Action;
    receipt.Seq = Seq;
    receipt.Irql = (ULONG)KeGetCurrentIrql();
    receipt.Status = Status;
    receipt.Refs = (ULONG)state;
    receipt.Ready = (state & (LONG64)NV_T214_DUMP_READY_BIT) ? 1u : 0u;
    receipt.Routine = Action == 1 ? (ULONG_PTR)&DumpPreInitialize : (ULONG_PTR)&DumpUninitialize;
#ifdef _NTDDK_
    LARGE_INTEGER sysTime, perfFreq;
    KeQuerySystemTime(&sysTime);
    receipt.SystemTime = (ULONGLONG)sysTime.QuadPart;
    receipt.PerfCounter = (ULONGLONG)KeQueryPerformanceCounter(&perfFreq).QuadPart;
    receipt.PerfFreq = (ULONGLONG)perfFreq.QuadPart;

    HANDLE svcKey = NULL, paramKey = NULL, prepKey = NULL;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attr;
    RtlInitUnicodeString(&name, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\nvtsdhc");
    InitializeObjectAttributes(&attr, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&svcKey, KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE, &attr))) {
        RtlInitUnicodeString(&name, L"Parameters");
        InitializeObjectAttributes(&attr, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, svcKey, NULL);
        if (NT_SUCCESS(ZwCreateKey(&paramKey, KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE, &attr, 0, NULL,
                                   REG_OPTION_NON_VOLATILE, NULL))) {
            RtlInitUnicodeString(&name, L"DumpPreparation");
            InitializeObjectAttributes(&attr, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, paramKey, NULL);
            if (NT_SUCCESS(ZwCreateKey(&prepKey, KEY_SET_VALUE, &attr, 0, NULL,
                                       REG_OPTION_NON_VOLATILE, NULL))) {
                RtlInitUnicodeString(&name, Action == 1 ? L"LastPrepare" : L"LastRelease");
                if (NT_SUCCESS(ZwSetValueKey(prepKey, &name, 0, REG_BINARY, &receipt, sizeof(receipt)))) {
                    ZwFlushKey(prepKey);
                }
            }
        }
    }
    if (prepKey != NULL) ZwClose(prepKey);
    if (paramKey != NULL) ZwClose(paramKey);
    if (svcKey != NULL) ZwClose(svcKey);
#else
    T214HostDumpPrepReceipt(&receipt);
#endif
}

/* crashdmp.sys invokes DumpPreInitialize at PASSIVE_LEVEL during boot so MMIO
 * mappings already exist when dumpsdport.sys calls SdhcSlotInitialize at HIGH_LEVEL. */
NV_DUMP_EXPORT
NTSTATUS
DumpPreInitialize(
    VOID
    )
{
    ULONG seq = (ULONG)InterlockedIncrement(&g_DumpPrepSeq);
    KIRQL irql = KeGetCurrentIrql();
    if (irql > DISPATCH_LEVEL) {
        /* Registration owns references only at an IRQL where its matching
         * uninitialize can release them. Dump I/O consumes the prepared map. */
        return STATUS_DEVICE_NOT_READY;
    }

    KIRQL oldIrql;
    NTSTATUS status = STATUS_SUCCESS;
    KeAcquireSpinLock(&g_DumpPrepLock, &oldIrql);
    LONG64 state = InterlockedCompareExchange64(&g_DumpPrepState, 0, 0);
    if (state & (LONG64)NV_T214_DUMP_READY_BIT) {
        InterlockedIncrement64(&g_DumpPrepState);
    } else {
        status = STATUS_INSUFFICIENT_RESOURCES;
        g_DumpMaps[0] = T214Map(0x60006000, 0x1000);
        g_DumpMaps[1] = T214Map(0x70000000, 0x4000);
        g_DumpMaps[2] = T214Map(0x7000e000, 0x1000);
        g_DumpMaps[3] = T214Map(0x7000d000, 0x1000);
        g_DumpMaps[4] = T214Map(0x6000d000, 0x1000);
        if (g_DumpMaps[0] && g_DumpMaps[1] && g_DumpMaps[2] && g_DumpMaps[3] && g_DumpMaps[4]) {
            if ((T214Read(g_DumpMaps[1], 0x804) & 0xfff0u) == 0x2120u) {
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_NOT_SUPPORTED;
            }
        }
        if (NT_SUCCESS(status)) {
            LONG64 next;
            do {
                state = InterlockedCompareExchange64(&g_DumpPrepState, 0, 0);
                next = ((state + 1) & 0xffffffffLL) | (LONG64)NV_T214_DUMP_READY_BIT;
            } while (InterlockedCompareExchange64(&g_DumpPrepState, next, state) != state);
        } else {
            T214UnmapDumpPrepared();
        }
    }
    KeReleaseSpinLock(&g_DumpPrepLock, oldIrql);
    if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
        T214RecordDumpPrepReceipt(1, status, seq);
    }
    return status;
}

NV_DUMP_EXPORT
VOID
DumpUninitialize(
    VOID
    )
{
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
        return;
    }
    ULONG seq = (ULONG)InterlockedIncrement(&g_DumpPrepSeq);
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_DumpPrepLock, &oldIrql);
    LONG64 state, next;
    do {
        state = InterlockedCompareExchange64(&g_DumpPrepState, 0, 0);
        ULONG refs = (ULONG)state;
        if (refs == 0) {
            next = 0;
            break;
        }
        refs -= 1;
        next = refs ? ((LONG64)refs | (state & (LONG64)NV_T214_DUMP_READY_BIT)) : 0;
    } while (InterlockedCompareExchange64(&g_DumpPrepState, next, state) != state);
    if ((ULONG)state != 0 && next == 0) {
        T214UnmapDumpPrepared();
    }
    KeReleaseSpinLock(&g_DumpPrepLock, oldIrql);
    if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
        T214RecordDumpPrepReceipt(2, STATUS_SUCCESS, seq);
    }
}

/* Resolve the currently prepared stack at the start of exclusive dump I/O.
 * Slot preparation can precede DumpPreInitialize. Do not cache pointers
 * across preparation/release of another dump or hibernation stack. */
static NTSTATUS T214InitializeDump(PSDHC_EXTENSION s)
{
    LONG64 state = InterlockedCompareExchange64(&g_DumpPrepState, 0, 0);
    if (!(state & (LONG64)NV_T214_DUMP_READY_BIT) || !(ULONG)state)
        return STATUS_DEVICE_NOT_READY;
    s->T214Car = g_DumpMaps[0];
    s->T214Misc = g_DumpMaps[1];
    s->T214Pmc = g_DumpMaps[2];
    s->T214I2c = g_DumpMaps[3];
    s->T214Gpio = g_DumpMaps[4];
    s->T214BorrowedDumpMaps = TRUE;
    s->T214DumpFreshPower = FALSE;
    return STATUS_SUCCESS;
}

/* Dump_Open can disable port events before asking for reset-all. */
static BOOLEAN T214DumpEventsAccessible(PSDHC_EXTENSION s)
{
    return s->T214BorrowedDumpMaps &&
           (InterlockedCompareExchange64(&g_DumpPrepState, 0, 0) & (LONG64)NV_T214_DUMP_READY_BIT) &&
           s->T214Car && s->T214Car == g_DumpMaps[0] &&
           (T214Read(s->T214Car, 0x10) & (1u << 14)) &&
           !(T214Read(s->T214Car, 0x04) & (1u << 14));
}

/* Called only for the dump stack's reset-all, never during preparation.
 * The live driver can have selected DDR200 after our extension was created.
 * Inspect CAR now, stop SDCLK before changing its source, and leave it off
 * until SDPORT sets the identification clock on the re-enumerated card. */
static NTSTATUS T214DumpTakeoverClock(PSDHC_EXTENSION s)
{
    const ULONG sdmmc1 = 1u << 14;
    /* dumpsdport can return the last reset's status after exhausting writes.
     * An unconfirmed last write must still fail at that recovery boundary. */
    if (s->T214DumpWriteFailures >= T214_DUMP_WRITE_ATTEMPTS) return STATUS_IO_TIMEOUT;
    NTSTATUS status = T214InitializeDump(s);
    if (!NT_SUCCESS(status)) return status;
    PUCHAR car = s->T214Car;
    BOOLEAN ready = (T214Read(car, 0x10) & sdmmc1) &&
                    !(T214Read(car, 0x04) & sdmmc1);
    if (ready) {
        T214Write(s->BaseAddress, SDHC_INTERRUPT_SIGNAL_ENABLE, 0);
        SdhcWriteRegisterUshort(s, SDHC_CLOCK_CONTROL, 0);
    }
    /* Abort inherited ADMA as well as card traffic before retiming the host.
     * Only SDMMC1 is held in reset; other CAR clients are untouched. */
    T214Write(car, 0x300, sdmmc1);
    if (!(T214Read(car, 0x04) & sdmmc1)) return STATUS_DEVICE_POWER_FAILURE;
    status = T214ParentClock(s);
    if (!NT_SUCCESS(status)) return status;
    if ((T214Read(car, 0x150) & 0xe00000ffu) != (3u << 29))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    T214Write(car, 0x320, sdmmc1);
    KeStallExecutionProcessor(2);
    if (!(T214Read(car, 0x10) & sdmmc1)) return STATUS_DEVICE_POWER_FAILURE;
    T214Write(car, 0x304, sdmmc1);
    KeStallExecutionProcessor(2);
    if (T214Read(car, 0x04) & sdmmc1) return STATUS_DEVICE_POWER_FAILURE;
    s->SpeedMode = SdhcSpeedModeNormal;
#if NV_T214_DDR200
    s->T214DdrActive = FALSE;
    s->T214DdrFatal = FALSE;
    s->T214DdrRecoverNeeded = FALSE;
    s->T214DdrAttempted = TRUE;
#endif
    return STATUS_SUCCESS;
}

static NTSTATUS T214ReadIdentitySector(PSDHC_EXTENSION s, ULONG Lba, PUCHAR Buffer)
{
    if (!T214WaitClear(s->BaseAddress, SDHC_PRESENT_STATE,
                      SDHC_PS_CMD_INHIBIT | SDHC_PS_DAT_INHIBIT, 100000))
        return STATUS_DEVICE_BUSY;
    T214Write(s->BaseAddress, SDHC_INTERRUPT_STATUS, 0xffffffffu);
    T214Write(s->BaseAddress, SDHC_INTERRUPT_STATUS_ENABLE, 0x037f8023u);
    SdhcWriteRegisterUchar(s, SDHC_TIMEOUT_CONTROL, SDHC_TC_MAX_DATA_TIMEOUT);
    SdhcWriteRegisterUshort(s, SDHC_BLOCK_SIZE, 512);
    SdhcWriteRegisterUshort(s, SDHC_BLOCK_COUNT, 1);
    SdhcWriteRegisterUshort(s, SDHC_TRANSFER_MODE, SDHC_TM_TRANSFER_READ | SDHC_TM_BLKCNT_ENABLE);
    SdhcWriteRegisterUlong(s, SDHC_ARGUMENT, Lba);
    SdhcWriteRegisterUshort(s, SDHC_COMMAND, (17u << 8) | SDHC_CMD_RESPONSE_48BIT_NOBUSY |
        SDHC_CMD_CRC_CHECK_ENABLE | SDHC_CMD_INDEX_CHECK_ENABLE | SDHC_CMD_DATA_PRESENT);
    BOOLEAN received = FALSE, commandDone = FALSE, transferDone = FALSE;
    for (ULONG elapsed = 0; elapsed < 1000000; elapsed += 5) {
        ULONG events = T214Read(s->BaseAddress, SDHC_INTERRUPT_STATUS);
        if (events & 0xffff8000u) return STATUS_IO_DEVICE_ERROR;
        if (events & SDHC_IS_CMD_COMPLETE) commandDone = TRUE;
        if (events & SDHC_IS_TRANSFER_COMPLETE) transferDone = TRUE;
        if ((events & SDHC_IS_BUFFER_READ_READY) && !received) {
            for (ULONG i = 0; i < 512; i += sizeof(ULONG)) {
                ULONG word = SdhcReadRegisterUlong(s, SDHC_DATA_PORT);
                RtlCopyMemory(Buffer + i, &word, sizeof(word));
            }
            received = TRUE;
        }
        SdhcWriteRegisterUshort(s, SDHC_INTERRUPT_STATUS, (USHORT)(events & 0x23u));
        if (received && commandDone && transferDone) return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    }
    return STATUS_IO_TIMEOUT;
}

/* SDPORT's dump writer can go straight to saved pagefile extents without
 * reading the partition tables. Validate the same physical MBR and NTFS
 * identity as runtime I/O before allowing its first CMD24/25. */
static NTSTATUS T214ValidateDumpTarget(PSDHC_EXTENSION s)
{
    if (!(s->TargetFacts & LiteSdxc) || s->AppPrefixAccepted)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (s->T214DmaActive || s->OutstandingRequests[0]) return STATUS_DEVICE_BUSY;
    ULONG signals = T214Read(s->BaseAddress, SDHC_INTERRUPT_SIGNAL_ENABLE);
    ULONG enables = T214Read(s->BaseAddress, SDHC_INTERRUPT_STATUS_ENABLE);
    UCHAR timeout = SdhcReadRegisterUchar(s, SDHC_TIMEOUT_CONTROL);
    ULONG next = s->NextReadLba;
    UCHAR block[512];
    SDPORT_COMMAND command = {0};
    command.BlockSize = 512;
    command.DataBuffer = block;
    T214Write(s->BaseAddress, SDHC_INTERRUPT_SIGNAL_ENABLE, 0);
    NTSTATUS status = T214ReadIdentitySector(s, 0, block);
    if (NT_SUCCESS(status)) {
        s->NextReadLba = 0;
        LiteObserveRead(s, &command);
        if (!(s->TargetFacts & LiteMbr)) status = STATUS_MEDIA_WRITE_PROTECTED;
    }
    if (NT_SUCCESS(status)) {
        status = T214ReadIdentitySector(s, WINARM_FIRST, block);
        if (NT_SUCCESS(status)) {
            s->NextReadLba = WINARM_FIRST;
            LiteObserveRead(s, &command);
            if ((s->TargetFacts & LiteWriteFacts) != LiteWriteFacts) status = STATUS_MEDIA_WRITE_PROTECTED;
        }
    }
    if (!NT_SUCCESS(status)) {
        InterlockedAnd(&s->TargetFacts, ~LiteWriteFacts);
        SdhcWriteRegisterUchar(s, SDHC_RESET, SDHC_RESET_CMD | SDHC_RESET_DAT);
        ULONG n;
        for (n = 0; n < 1000; n += 5) {
            if (!(SdhcReadRegisterUchar(s, SDHC_RESET) & (SDHC_RESET_CMD | SDHC_RESET_DAT))) break;
            KeStallExecutionProcessor(5);
        }
        if (n == 1000) status = STATUS_IO_TIMEOUT;
    }
    s->NextReadLba = next;
    SdhcWriteRegisterUchar(s, SDHC_TIMEOUT_CONTROL, timeout);
    T214Write(s->BaseAddress, SDHC_INTERRUPT_STATUS, 0xffff00e3u);
    T214Write(s->BaseAddress, SDHC_INTERRUPT_STATUS_ENABLE, enables);
    T214Write(s->BaseAddress, SDHC_INTERRUPT_SIGNAL_ENABLE, signals);
    return status;
}

static NTSTATUS T214PrepareDumpWrite(PSDHC_EXTENSION s, PSDPORT_REQUEST r)
{
    if (!s->CrashdumpMode || (r->Command.Index != 24 && r->Command.Index != 25))
        return STATUS_SUCCESS;
    if (r->Type != SdRequestTypeCommandWithTransfer ||
        !LiteBlocksInExtent(r->Command.Argument, r->Command.BlockCount, WINARM_FIRST, WINARM_COUNT) ||
        (s->TargetFacts & LiteWriteFacts) == LiteWriteFacts)
        return STATUS_SUCCESS;
    return T214ValidateDumpTarget(s);
}
