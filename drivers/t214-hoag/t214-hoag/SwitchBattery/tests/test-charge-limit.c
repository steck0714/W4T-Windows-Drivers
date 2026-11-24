/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef uint8_t UCHAR, *PUCHAR, BOOLEAN;
typedef uint16_t USHORT, *PUSHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t NTSTATUS;
typedef const wchar_t *PCWSTR;
typedef void VOID, *PVOID, *WDFIOTARGET;

#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009Cu)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3u)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185u)
#define RtlZeroMemory(dst, len) memset((dst), 0, (len))

#define GAUGE_REPSOC    0x06
#define GAUGE_VCELL     0x09
#define BQ_INPUT_SRC    0x00
#define BQ_PWR_ON       0x01
#define BQ_CHARGE_CURRENT 0x02
#define BQ_TERM_TIMER   0x05
#define CHARGER_SYSSTAT 0x08
#define BQ_HIZ          0x80u
#define BQ_RESET_BITS   0xC0u
#define BQ_MODE_MASK    0x30u
#define BQ_MODE_CHARGE  0x10u
#define BQ_MODE_OTG     0x20u

typedef enum _USBC_STATE { UsbcIdle = 0, UsbcHost = 1, UsbcSink = 2 } USBC_STATE;

typedef struct _SWBAT_DEVICE {
    WDFIOTARGET Gauge;
    WDFIOTARGET Charger;
    BOOLEAN Stopping;
    USBC_STATE Usbc;
    ULONG ChargeLimit;
    BOOLEAN ChargeInhibited;
    BOOLEAN ChargeLimitInitialized;
    BOOLEAN SuspendChargeDisabled;
    NTSTATUS LastLimitStatus;
} SWBAT_DEVICE, *PSWBAT_DEVICE;

static uint8_t g_bq[16];
static uint16_t g_soc_raw;
static uint16_t g_vcell_raw;
static unsigned g_bq_writes;
static int g_fail_write;
static int g_corrupt_verify;

static VOID SwBatTrace(PCWSTR Name, ULONG Value)
{
    (void)Name;
    (void)Value;
}

static NTSTATUS SwBatReadRegs(WDFIOTARGET Target, UCHAR Register, PUCHAR Data, ULONG Length)
{
    assert(Target != NULL && Length == 1 && Register < sizeof(g_bq));
    *Data = g_bq[Register];
    if (g_corrupt_verify && Register == BQ_PWR_ON && g_bq_writes > 0) {
        *Data ^= BQ_MODE_CHARGE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS SwBatRead16(WDFIOTARGET Target, UCHAR Register, PUSHORT Value)
{
    assert(Target != NULL);
    if (Register == GAUGE_REPSOC) {
        *Value = g_soc_raw;
        return STATUS_SUCCESS;
    }
    if (Register == GAUGE_VCELL) {
        *Value = g_vcell_raw;
        return STATUS_SUCCESS;
    }
    assert(0 && "unexpected gauge register");
    return STATUS_INVALID_PARAMETER;
}

static NTSTATUS SwBatWrite8(WDFIOTARGET Target, UCHAR Register, UCHAR Value)
{
    assert(Target != NULL && Register == BQ_PWR_ON);
    if (g_fail_write) {
        return STATUS_IO_DEVICE_ERROR;
    }
    g_bq[Register] = Value;
    ++g_bq_writes;
    return STATUS_SUCCESS;
}

static NTSTATUS SwBatSetBqMode(PSWBAT_DEVICE Dev, UCHAR Mode)
{
    UCHAR Pwr = 0, Verify = 0;
    NTSTATUS Status = SwBatReadRegs(Dev->Charger, BQ_PWR_ON, &Pwr, 1);
    if (NT_SUCCESS(Status)) {
        Status = SwBatWrite8(Dev->Charger, BQ_PWR_ON, (UCHAR)((Pwr & ~(BQ_RESET_BITS | BQ_MODE_MASK)) | Mode));
    }
    if (NT_SUCCESS(Status)) {
        Status = SwBatReadRegs(Dev->Charger, BQ_PWR_ON, &Verify, 1);
    }
    if (NT_SUCCESS(Status) && (Verify & BQ_MODE_MASK) != Mode) {
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    return Status;
}

#include "../src/charge_limit.h"

static void reset_fixture(SWBAT_DEVICE *d, unsigned soc_pct)
{
    memset(d, 0, sizeof(*d));
    memset(g_bq, 0, sizeof(g_bq));
    d->Gauge = d;
    d->Charger = d;
    g_bq[BQ_INPUT_SRC] = 0x36;       /* VINDPM + 2A IINLIM, EN_HIZ = 0 */
    g_bq[BQ_PWR_ON] = 0x1B;          /* CHG_CONFIG = 0x10, SYS_MIN + BOOST_LIM */
    g_bq[BQ_CHARGE_CURRENT] = 0x60;
    g_bq[BQ_TERM_TIMER] = 0x8A;      /* EN_TERM + 8h safety timer */
    g_bq[CHARGER_SYSSTAT] = 0xA4;    /* Adapter VBUS + Fast charge + PG */
    g_soc_raw = (uint16_t)(soc_pct << 8);
    g_vcell_raw = (uint16_t)((4100u * 1000u / 625u) << 3);
    g_bq_writes = 0;
    g_fail_write = 0;
    g_corrupt_verify = 0;
}

static void assert_non_mode_regs_intact(void)
{
    assert(g_bq[BQ_INPUT_SRC] == 0x36);          /* EN_HIZ (0x80) never set, IINLIM intact */
    assert((g_bq[BQ_PWR_ON] & 0x0Fu) == 0x0Bu);  /* SYS_MIN / BOOST_LIM intact */
    assert(g_bq[BQ_CHARGE_CURRENT] == 0x60);
    assert(g_bq[BQ_TERM_TIMER] == 0x8A);         /* 8h safety timer + EN_TERM intact */
}

int main(void)
{
    SWBAT_DEVICE d;
    SWBAT_CHARGE_LIMIT_STATUS st;
    BOOLEAN notify = FALSE;

    /* 1. Default Off (0): zero writes when already in BQ_MODE_CHARGE. */
    reset_fixture(&d, 75);
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(g_bq_writes == 0 && !d.ChargeInhibited);
    assert_non_mode_regs_intact();

    /* 2. Reject invalid thresholds without touching hardware. */
    const ULONG bad_limits[] = { 1, 50, 54, 56, 60, 80, 84, 86, 90, 100, 500 };
    for (size_t i = 0; i < sizeof(bad_limits) / sizeof(bad_limits[0]); ++i) {
        assert(SwBatSetChargeLimitValue(&d, bad_limits[i], &notify) == STATUS_INVALID_PARAMETER);
        assert(d.ChargeLimit == SWBAT_CHARGE_LIMIT_OFF && g_bq_writes == 0);
    }

    /* 3. 55% center -> 50..60% hysteresis band, no HIZ even above 60%. */
    reset_fixture(&d, 48);
    assert(SwBatSetChargeLimitValue(&d, SWBAT_CHARGE_LIMIT_DOCKED, &notify) == STATUS_SUCCESS);
    assert(notify && d.ChargeLimit == 55 && !d.ChargeInhibited);
    for (unsigned soc = 49; soc <= 59; ++soc) {
        g_soc_raw = (uint16_t)(soc << 8);
        assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
        assert(!d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);
        assert_non_mode_regs_intact();
    }
    for (unsigned soc = 60; soc <= 72; ++soc) {
        g_soc_raw = (uint16_t)(soc << 8);
        assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
        assert(d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_DISABLED);
        assert_non_mode_regs_intact();
    }
    g_bq[CHARGER_SYSSTAT] = 0x84; /* Adapter VBUS + PG, CHRG_STAT = 0 (not charging) */
    assert(SwBatQueryChargeLimitStatus(&d, &st) == STATUS_SUCCESS);
    assert(st.LimitCenter == 55 && st.BandMin == 50 && st.BandMax == 60);
    assert((st.Flags & SWBAT_CHARGE_FLAG_INHIBITED) != 0);
    assert((st.Flags & SWBAT_CHARGE_FLAG_EXTERNAL_ONLINE) != 0);
    assert((st.Flags & SWBAT_CHARGE_FLAG_CHARGING) == 0);
    assert((st.Flags & SWBAT_CHARGE_FLAG_HIZ_ACTIVE) == 0);

    for (unsigned soc = 59; soc >= 51; --soc) {
        g_soc_raw = (uint16_t)(soc << 8);
        assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
        assert(d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_DISABLED);
    }
    g_soc_raw = (uint16_t)(50u << 8);
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(!d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);

    /* 4. 85% center -> 80..90% hysteresis band + Sink plug-in re-enforcement. */
    g_soc_raw = (uint16_t)(84u << 8);
    assert(SwBatSetChargeLimitValue(&d, SWBAT_CHARGE_LIMIT_HIGH, &notify) == STATUS_SUCCESS);
    assert(!d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);
    g_soc_raw = (uint16_t)(90u << 8);
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_DISABLED);
    /* Simulate SwBatConfigureSink setting BQ_MODE_CHARGE on charger plug-in at 93%. */
    g_soc_raw = (uint16_t)(93u << 8);
    g_bq[BQ_PWR_ON] = (uint8_t)((g_bq[BQ_PWR_ON] & ~BQ_MODE_MASK) | BQ_MODE_CHARGE);
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_DISABLED);
    assert_non_mode_regs_intact();

    /* 5. Suspend disables charging without losing hysteresis state; wake restores. */
    reset_fixture(&d, 84);
    assert(SwBatSetChargeLimitValue(&d, SWBAT_CHARGE_LIMIT_HIGH, &notify) == STATUS_SUCCESS);
    assert(!d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);
    assert(SwBatSuspendChargeLimit(&d) == STATUS_SUCCESS);
    assert(d.SuspendChargeDisabled && !d.ChargeInhibited);
    assert((g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_DISABLED);
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(!d.SuspendChargeDisabled && !d.ChargeInhibited);
    assert((g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);

    /* 6. OTG VBUS boost is never clobbered by Apply or Suspend. */
    d.Usbc = UsbcHost;
    g_bq[BQ_PWR_ON] = 0x2B; /* BQ_MODE_OTG */
    g_soc_raw = (uint16_t)(95u << 8);
    g_bq_writes = 0;
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(SwBatSuspendChargeLimit(&d) == STATUS_SUCCESS);
    assert(g_bq_writes == 0 && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_OTG);

    /* 7. Warm reboot inside band with REG01=0x00 preserves inhibit; 0% SoC never inhibits. */
    reset_fixture(&d, 56);
    d.ChargeLimit = SWBAT_CHARGE_LIMIT_DOCKED;
    g_bq[BQ_PWR_ON] = 0x0B; /* already disabled before reboot */
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(d.ChargeInhibited && g_bq_writes == 0);
    g_soc_raw = 0;
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(!d.ChargeInhibited && (g_bq[BQ_PWR_ON] & BQ_MODE_MASK) == BQ_MODE_CHARGE);

    /* 8. I2C write failure & readback mismatch roll back SetChargeLimitValue. */
    reset_fixture(&d, 95);
    g_fail_write = 1;
    assert(SwBatSetChargeLimitValue(&d, SWBAT_CHARGE_LIMIT_HIGH, &notify) == STATUS_IO_DEVICE_ERROR);
    assert(d.ChargeLimit == SWBAT_CHARGE_LIMIT_OFF && !d.ChargeInhibited);

    reset_fixture(&d, 95);
    g_corrupt_verify = 1;
    assert(SwBatSetChargeLimitValue(&d, SWBAT_CHARGE_LIMIT_HIGH, &notify) == STATUS_DEVICE_DATA_ERROR);
    assert(d.ChargeLimit == SWBAT_CHARGE_LIMIT_OFF && !d.ChargeInhibited);

    reset_fixture(&d, 95);
    d.ChargeLimit = 123; /* invalid persisted state follows the 85% baseline */
    assert(SwBatRefreshChargeLimit(&d) == STATUS_SUCCESS);
    assert(d.ChargeLimit == SWBAT_CHARGE_LIMIT_HIGH && d.ChargeInhibited);
    assert_non_mode_regs_intact();
    puts("Battery charge limit: 50-60/80-90 hysteresis, zero-HIZ, OTG, suspend/wake and fault rollback passed");
    return 0;
}
