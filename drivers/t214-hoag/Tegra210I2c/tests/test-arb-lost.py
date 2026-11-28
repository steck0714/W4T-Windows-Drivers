#!/usr/bin/env python3
"""Exercise Tegra210I2c I2C5 DFLL multi-master INT_ARB_LOST FIFO reset, backoff and sequence replay."""
import os
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/tegra210i2c.c').read_text()

def definition(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    return source[start:source.index('\n}', brace) + 2]

stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef void VOID, *PVOID;
typedef bool BOOLEAN;
typedef uint8_t UCHAR, *PUCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG;
typedef int32_t NTSTATUS;
typedef size_t SIZE_T;
typedef const wchar_t *PCWSTR;

#define TRUE true
#define FALSE false
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_NO_SUCH_DEVICE ((NTSTATUS)0xC000000Eu)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009Au)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5u)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185u)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023u)
#define NT_SUCCESS(x) ((x) >= 0)
#define RtlCopyMemory memcpy
#define POOL_FLAG_NON_PAGED 64u
#define NormalPagePriority 16u
#define MdlMappingNoExecute 0x40000000u

#define I2C_CNFG            0x000
#define I2C_STATUS          0x01C
#define I2C_TX_FIFO         0x050
#define I2C_RX_FIFO         0x054
#define I2C_FIFO_CONTROL    0x05C
#define I2C_FIFO_STATUS     0x060
#define I2C_INT_MASK        0x064
#define I2C_INT_STATUS      0x068
#define I2C_CLK_DIVISOR     0x06C
#define I2C_BUS_CLEAR_CONFIG 0x084
#define I2C_BUS_CLEAR_STATUS 0x088
#define I2C_CONFIG_LOAD     0x08C

#define FIFO_FLUSH_TX_RX    0x3u
#define INT_ARB_LOST        (1u << 2)
#define INT_NO_ACK          (1u << 3)
#define INT_ERR_MASK        0x3Cu
#define INT_ALL_PACKETS     (1u << 6)
#define INT_PACKET_DONE     (1u << 7)
#define INT_BUS_CLEAR_DONE  (1u << 11)
#define HDR0_I2C_PACKET1    0x10010u
#define HDR_REPEAT_START    (1u << 16)
#define HDR_READ            (1u << 19)
#define CONFIG_LOAD_ALL     0x25u
#define I2C_MAX_TRANSFER    4096
#define I2C5_PHYS_BASE      0x7000D000u
#define I2C5_ARB_MAX_RETRIES 4u
#define I2C5_ARB_BACKOFF_US 50u

typedef struct _MDL {
    struct _MDL *Next;
    PUCHAR Va;
    SIZE_T ByteCount;
} MDL, *PMDL;

typedef enum {
    SpbTransferDirectionToDevice = 1,
    SpbTransferDirectionFromDevice = 2,
} SPB_TRANSFER_DIRECTION;

typedef struct {
    SPB_TRANSFER_DIRECTION Direction;
    SIZE_T TransferLength;
    ULONG DelayInUs;
} SPB_TRANSFER_DESCRIPTOR;

typedef struct _TEGRA_I2C_DEVICE {
    volatile UCHAR *Base;
    ULONG Length;
    PVOID SdGate;
    PVOID SdGateHandle;
    ULONG PhysLow;
    ULONG Hidrev;
    volatile LONG Errors, Nacks, Timeouts, ArbLostRetries, LastError, LastAddress;
} TEGRA_I2C_DEVICE, *PTEGRA_I2C_DEVICE;

typedef struct _TEGRA_I2C_TARGET {
    USHORT Address;
    ULONG Speed;
} TEGRA_I2C_TARGET, *PTEGRA_I2C_TARGET;

typedef PTEGRA_I2C_DEVICE WDFDEVICE;
typedef PTEGRA_I2C_TARGET SPBTARGET;
typedef struct MockRequest *SPBREQUEST;
typedef SPBREQUEST WDFREQUEST;

struct MockTransfer {
    SPB_TRANSFER_DIRECTION dir;
    MDL mdl;
    ULONG delay_us;
};

struct MockRequest {
    struct MockTransfer xfers[4];
    ULONG count;
    SIZE_T info;
    NTSTATUS status;
    BOOLEAN completed;
};

#define TegraI2cGetDevice(x) (x)
#define TegraI2cGetTarget(x) (x)
#define SPB_TRANSFER_DESCRIPTOR_INIT(d) memset((d), 0, sizeof(*(d)))
#define MmGetSystemAddressForMdlSafe(m, p) ((void)(p), (m)->Va)
#define MmGetMdlByteCount(m) ((m)->ByteCount)
#define InterlockedIncrement(p) (++(*(p)))
#define InterlockedExchange(p, v) (*(p) = (v))

static struct {
    ULONG int_status;
    ULONG fifo_control;
    ULONG config_load;
    ULONG bus_clears;
    ULONG fifo_resets;
    ULONG tx_hdr_idx;
    ULONG tx_hdr[3];
    ULONG stalls[64];
    ULONG stall_count;
    ULONG total_stall_us;
    ULONG packet_index;
    ULONG arb_lost_mask;     /* bitmask of packet_index values that lose arbitration */
    ULONG nack_mask;         /* bitmask of packet_index values that NACK */
    BOOLEAN timeout_packet;
    BOOLEAN fifo_flush_stuck;
    UCHAR pmic_ptr;
    UCHAR pmic_regs[256];
    ULONG gate_acqs, gate_rels;
    ULONG traced_arb_retries;
    ULONG traced_selftest;
} bus;

static void reset_bus(void) {
    memset(&bus, 0, sizeof(bus));
    bus.pmic_regs[0x23] = 0x6Eu; /* GPU M1 = 800 mV */
    bus.pmic_regs[0x26] = 0x99u; /* CPU M0 = DFLL rail */
    bus.pmic_regs[0x15] = 0x42u;
}

static void KeStallExecutionProcessor(ULONG us) {
    if (bus.stall_count < 64) {
        bus.stalls[bus.stall_count++] = us;
    }
    bus.total_stall_us += us;
}

static ULONG RdReg(PTEGRA_I2C_DEVICE Dev, ULONG Off) {
    (void)Dev;
    if (Off == I2C_INT_STATUS) return bus.int_status;
    if (Off == I2C_FIFO_CONTROL) return bus.fifo_control;
    if (Off == I2C_CONFIG_LOAD) return bus.config_load;
    if (Off == I2C_FIFO_STATUS) {
        if (bus.timeout_packet) return 0;
        return (8u << 4) | 1u; /* TX empty slots + RX word ready */
    }
    if (Off == I2C_RX_FIFO) {
        bus.int_status |= INT_PACKET_DONE | INT_ALL_PACKETS;
        return bus.pmic_regs[bus.pmic_ptr];
    }
    return 0;
}

static VOID WrReg(PTEGRA_I2C_DEVICE Dev, ULONG Off, ULONG Value) {
    (void)Dev;
    if (Off == I2C_INT_STATUS) {
        bus.int_status &= ~Value;
        return;
    }
    if (Off == I2C_FIFO_CONTROL) {
        if (Value & FIFO_FLUSH_TX_RX) {
            ++bus.fifo_resets;
            bus.tx_hdr_idx = 0;
            bus.fifo_control = bus.fifo_flush_stuck ? FIFO_FLUSH_TX_RX : 0;
        }
        return;
    }
    if (Off == I2C_CONFIG_LOAD) {
        bus.config_load = 0;
        return;
    }
    if (Off == I2C_BUS_CLEAR_CONFIG) {
        ++bus.bus_clears;
        bus.int_status |= INT_BUS_CLEAR_DONE;
        return;
    }
    if (Off == I2C_TX_FIFO) {
        if (bus.tx_hdr_idx < 3) {
            bus.tx_hdr[bus.tx_hdr_idx++] = Value;
            if (bus.tx_hdr_idx == 3) {
                ULONG pkt = bus.packet_index++;
                if ((bus.arb_lost_mask >> pkt) & 1u) {
                    /* DFLL won arbitration on PWR_I2C and rewrote MAX77812 reg pointer to 0x26!
                     * Real Tegra210 packet mode also asserts INT_PACKET_DONE alongside ARB_LOST. */
                    bus.pmic_ptr = 0x26u;
                    bus.int_status = INT_ARB_LOST | INT_PACKET_DONE;
                    if ((bus.nack_mask >> pkt) & 1u) {
                        bus.int_status |= INT_NO_ACK;
                    }
                } else if ((bus.nack_mask >> pkt) & 1u) {
                    bus.int_status = INT_NO_ACK | INT_PACKET_DONE;
                }
            }
        } else {
            ULONG len = bus.tx_hdr[1] + 1u;
            bus.pmic_ptr = (UCHAR)(Value & 0xFFu);
            if (len > 1u) {
                bus.pmic_regs[bus.pmic_ptr] = (UCHAR)((Value >> 8) & 0xFFu);
            }
            if (!bus.timeout_packet && (bus.int_status & INT_ERR_MASK) == 0) {
                bus.int_status |= INT_PACKET_DONE | INT_ALL_PACKETS;
            }
        }
    }
}

static VOID TegraI2cTrace(PTEGRA_I2C_DEVICE Dev, PCWSTR Name, ULONG Value) {
    (void)Dev;
    if (!wcscmp(Name, L"ArbLostRetries")) bus.traced_arb_retries = Value;
    if (wcsncmp(Name, L"SelfTest_", 9) == 0) bus.traced_selftest = Value;
}

static VOID TegraI2cTraceErrors(PTEGRA_I2C_DEVICE Dev) {
    TegraI2cTrace(Dev, L"ArbLostRetries", (ULONG)Dev->ArbLostRetries);
}

static NTSTATUS TegraI2c5Acquire(PVOID gate) { (void)gate; ++bus.gate_acqs; return STATUS_SUCCESS; }
static VOID TegraI2c5Release(PVOID gate) { (void)gate; ++bus.gate_rels; }
static PVOID ExAllocatePool2(ULONG flags, SIZE_T len, ULONG tag) {
    (void)flags; (void)tag; return calloc(1, len);
}
static VOID ExFreePoolWithTag(PVOID p, ULONG tag) { (void)tag; free(p); }
static VOID SpbRequestGetTransferParameters(SPBREQUEST req, ULONG idx, SPB_TRANSFER_DESCRIPTOR *desc, PMDL *mdl) {
    assert(idx < req->count);
    desc->Direction = req->xfers[idx].dir;
    desc->TransferLength = req->xfers[idx].mdl.ByteCount;
    desc->DelayInUs = req->xfers[idx].delay_us;
    *mdl = &req->xfers[idx].mdl;
}
static VOID WdfRequestSetInformation(WDFREQUEST req, SIZE_T total) { req->info = total; }
static VOID SpbRequestComplete(SPBREQUEST req, NTSTATUS status) { req->status = status; req->completed = TRUE; }
'''

tests = r'''
int main(void) {
    TEGRA_I2C_DEVICE dev;
    TEGRA_I2C_TARGET tgt = {.Address = 0x33, .Speed = 400000};
    UCHAR reg = 0x23, val = 0;
    struct MockRequest req;

    /* 1. I2C5 2-packet RepeatStart read: packet 0 (Write reg) loses arbitration,
     *    packet 1 (Read) on retry 1 also loses arbitration (DFLL sets ptr=0x26!),
     *    retry 2 replays from packet 0 (Write 0x23 -> Read 0x6E) with 0 BusClear! */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    /* Packet 0 fails (attempt 0, idx 0 -> 1 pkt), then attempt 1 pkt 1 succeeds & pkt 2 fails! */
    bus.arb_lost_mask = (1u << 0) | (1u << 2);
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, &reg, 1}, 0};
    req.xfers[1] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 2);
    assert(req.completed && req.status == STATUS_SUCCESS && req.info == 2);
    assert(val == 0x6Eu); /* Proves full sequence replayed reg 0x23 instead of reading DFLL's 0x26! */
    assert(bus.bus_clears == 0);
    assert(dev.Errors == 0 && dev.ArbLostRetries == 2 && bus.traced_arb_retries == 2);
    assert(bus.stall_count == 2 && bus.stalls[0] == 50u && bus.stalls[1] == 100u);
    assert(bus.gate_acqs == 1 && bus.gate_rels == 1);

    /* 2. Exhausting all 1 + I2C5_ARB_MAX_RETRIES attempts on I2C5 returns
     *    STATUS_IO_DEVICE_ERROR, records 1 Error + 4 ArbLostRetries, and STILL
     *    issues ZERO destructive BusClear clocks onto live DFLL PWR_I2C! */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = 0x3FFu;
    memset(&req, 0, sizeof(req));
    UCHAR write_cmd[2] = {0x23, 0x6E};
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && req.info == 0);
    assert(bus.bus_clears == 0);
    assert(dev.Errors == 1 && dev.ArbLostRetries == 4 &&
           dev.LastError == (INT_ARB_LOST | INT_PACKET_DONE) && dev.LastAddress == 0x33);
    assert(bus.stall_count == 4 && bus.stalls[0] == 50u && bus.stalls[1] == 100u &&
           bus.stalls[2] == 150u && bus.stalls[3] == 200u);

    /* 3. Single-master bus (I2C1 0x7000c000): INT_ARB_LOST means stuck slave,
     *    so it does NOT retry and immediately performs BusClear recovery. */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = 0x7000C000u;
    reset_bus();
    bus.arb_lost_mask = 1u;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR);
    assert(bus.bus_clears == 1 && dev.Errors == 1 && dev.ArbLostRetries == 0 && bus.stall_count == 0);

    /* 4. INT_NO_ACK and combined (INT_ARB_LOST | INT_NO_ACK) on I2C5 return
     *    STATUS_NO_SUCH_DEVICE without retry or BusClear. */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.nack_mask = 1u;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_NO_SUCH_DEVICE);
    assert(bus.bus_clears == 0 && dev.Nacks == 1 && dev.Errors == 1 && dev.ArbLostRetries == 0);

    reset_bus();
    bus.arb_lost_mask = 1u;
    bus.nack_mask = 1u;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_NO_SUCH_DEVICE);
    assert(bus.bus_clears == 0 && dev.Nacks == 2 && dev.Errors == 2 && dev.ArbLostRetries == 0);

    /* 5. Stuck FIFO flush on I2C5 escalates to STATUS_IO_TIMEOUT with 0 BusClear clocks. */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.fifo_flush_stuck = TRUE;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_TIMEOUT && bus.bus_clears == 0 && dev.Timeouts == 1);

    /* 6. Genuine bus hang timeout on I2C5 skips BusClear (DFLL shares PWR_I2C),
     *    while single-master I2C1 still executes BusClear on timeout. */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.timeout_packet = TRUE;
    val = 0;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_TIMEOUT && bus.bus_clears == 0 && dev.Timeouts == 1);

    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = 0x7000C000u;
    reset_bus();
    bus.timeout_packet = TRUE;
    val = 0;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_TIMEOUT && bus.bus_clears == 1 && dev.Timeouts == 1);

    /* 7. Multi-write sequence on I2C5: once packet 0 has committed a write,
     *    arbitration loss on packet 1 must NOT replay packet 0 (no duplicate side effects). */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = (1u << 1); /* Packet 0 commits, packet 1 loses arbitration */
    UCHAR write_cmd2[2] = {0x06, 0x55};
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd, 2}, 0};
    req.xfers[1] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, write_cmd2, 2}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 2);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && req.info == 2);
    assert(bus.packet_index == 2 && bus.bus_clears == 0 && dev.ArbLostRetries == 0 && dev.Errors == 1);

    /* 8. M6 regression: 5-byte single-packet write on I2C5 must NOT auto-replay
     *    on INT_ARB_LOST (data-phase arbitration loss could duplicate a committed prefix). */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = 1u;
    UCHAR five_bytes[5] = {0x23, 0x11, 0x22, 0x33, 0x44};
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, five_bytes, 5}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 1);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && req.info == 0);
    assert(bus.packet_index == 1 && bus.bus_clears == 0 && dev.ArbLostRetries == 0 && dev.Errors == 1);

    /* 9. Read-to-clear IRQ registers on MAX77620 (0x3C:0x05) AND MAX77812
     *    (0x33:0x20 BUCK_INT R/C) AND unlisted registers must NOT auto-replay (A02/T8). */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = (1u << 1);
    TEGRA_I2C_TARGET pmic3c = {.Address = 0x3C, .Speed = 400000};
    UCHAR irq_reg = 0x05u;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, &irq_reg, 1}, 0};
    req.xfers[1] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
    TegraI2cRunRequest(&dev, &pmic3c, &req, 2);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && req.info == 1);
    assert(bus.packet_index == 2 && bus.bus_clears == 0 && dev.ArbLostRetries == 0);

    /* T8: MAX77812 0x33:0x20 BUCK_INT (R/C) must NOT auto-replay on INT_ARB_LOST! */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = (1u << 1);
    UCHAR buck_int = 0x20u;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, &buck_int, 1}, 0};
    req.xfers[1] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
    TegraI2cRunRequest(&dev, &tgt, &req, 2);
    assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && req.info == 1);
    assert(bus.packet_index == 2 && bus.bus_clears == 0 && dev.ArbLostRetries == 0);

    /* Unknown / unlisted registers (0x33:0x55, 0x3C:0x77, 0x1B:0x05) must NOT auto-replay! */
    {
        const USHORT bad_addrs[3] = {0x33u, 0x3Cu, 0x1Bu};
        const UCHAR bad_regs[3] = {0x55u, 0x77u, 0x05u};
        for (int k = 0; k < 3; ++k) {
            memset(&dev, 0, sizeof(dev));
            dev.PhysLow = I2C5_PHYS_BASE;
            reset_bus();
            bus.arb_lost_mask = (1u << 1);
            TEGRA_I2C_TARGET t = {.Address = bad_addrs[k], .Speed = 400000};
            UCHAR r = bad_regs[k];
            memset(&req, 0, sizeof(req));
            req.count = 2;
            req.xfers[0] = (struct MockTransfer){SpbTransferDirectionToDevice, {NULL, &r, 1}, 0};
            req.xfers[1] = (struct MockTransfer){SpbTransferDirectionFromDevice, {NULL, &val, 1}, 0};
            TegraI2cRunRequest(&dev, &t, &req, 2);
            assert(req.completed && req.status == STATUS_IO_DEVICE_ERROR && dev.ArbLostRetries == 0);
        }
    }

    /* 10. D0Entry TegraI2cSelfTest on I2C5 replays full register read across DFLL INT_ARB_LOST. */
    memset(&dev, 0, sizeof(dev));
    dev.PhysLow = I2C5_PHYS_BASE;
    reset_bus();
    bus.arb_lost_mask = (1u << 1); /* Lose arbitration on Read packet after Write reg succeeded! */
    TegraI2cSelfTest(&dev, 0x3C, 0x15, L"SelfTest_MAX77620_OnOffStat");
    assert(bus.traced_selftest == (0x100u | 0x42u) && bus.bus_clears == 0 && dev.ArbLostRetries == 1);
    return 0;
}
'''

extracted = '\n\n'.join([
    definition('static BOOLEAN TegraI2cWaitClear('),
    definition('static BOOLEAN TegraI2cLoadConfig('),
    definition('static BOOLEAN TegraI2cResetFifos('),
    definition('static VOID TegraI2cBusClear('),
    definition('static NTSTATUS TegraI2cTransfer('),
    definition('static NTSTATUS TegraI2cMdlCopy('),
    definition('static BOOLEAN TegraI2cIsSafeI2c5Retry('),
    definition('static VOID TegraI2cRunRequest('),
    definition('static VOID TegraI2cSelfTest('),
])

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    case = root / 'arb_lost.c'
    case.write_text(stubs + '\n' + extracted + '\n' + tests)
    exe = root / 'arb_lost'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-Wall', '-Wextra', '-Werror', str(case), '-o', str(exe)], check=True)
    subprocess.run([exe], check=True)
print('PASS: Tegra210I2c I2C5 DFLL multi-master INT_ARB_LOST FIFO flush, backoff, full-sequence replay and 0-BusClear verification')
