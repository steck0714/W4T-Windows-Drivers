/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * BQ24193 SoC charging limit regulation for SwitchBattery.
 *
 * Behavioral reference: Switch L4T kernel 4.9 commit
 * 2d0059fd3167a8df756de2aa0489d4aa70a9fc15
 * (drivers/power/supply/bq2419x-charger.c: bq2419x_soc_regulation_control,
 * bq2419x_set_property, bq2419x_suspend, bq2419x_resume).
 *
 * Platform difference on Windows:
 *   Linux sets BQ2419X_EN_HIZ (REG00 bit 7) above limit_max to disconnect
 *   VBUS and actively drain the battery down to limit_max. On Windows, cutting
 *   external input power is undesirable for stability; above BandMax (60% or
 *   90%) we inhibit battery charging via REG01 CHG_CONFIG = 0x00 while keeping
 *   REG00 EN_HIZ = 0 so external VBUS power continues feeding VSYS.
 */
#ifndef SWITCHBATTERY_CHARGE_LIMIT_H
#define SWITCHBATTERY_CHARGE_LIMIT_H

#include "charge_limit_ioctl.h"

#define BQ_MODE_DISABLED 0x00u

static __inline BOOLEAN SwBatChargeLimitValid(ULONG Limit)
{
    return Limit == SWBAT_CHARGE_LIMIT_OFF ||
           Limit == SWBAT_CHARGE_LIMIT_DOCKED ||
           Limit == SWBAT_CHARGE_LIMIT_HIGH;
}

static __inline VOID SwBatChargeLimitBands(ULONG Limit, PULONG BandMin, PULONG BandMax)
{
    if (Limit == SWBAT_CHARGE_LIMIT_DOCKED || Limit == SWBAT_CHARGE_LIMIT_HIGH) {
        *BandMin = Limit - SWBAT_CHARGE_LIMIT_HYSTERESIS;
        *BandMax = Limit + SWBAT_CHARGE_LIMIT_HYSTERESIS;
    } else {
        *BandMin = 0;
        *BandMax = 100;
    }
}

/* Enforce SoC hysteresis on BQ24193 REG01. Caller holds Dev->Lock. */
static NTSTATUS SwBatApplyChargeLimit(PSWBAT_DEVICE Dev, ULONG SocPercent)
{
    UCHAR Pwr = 0;
    UCHAR DesiredMode;
    ULONG BandMin = 0, BandMax = 100;
    BOOLEAN NextInhibited;
    NTSTATUS Status;

    if (!SwBatChargeLimitValid(Dev->ChargeLimit)) {
        Dev->ChargeLimit = SWBAT_CHARGE_LIMIT_HIGH;
    }
    if (Dev->Charger == NULL) {
        if (Dev->ChargeLimit == SWBAT_CHARGE_LIMIT_OFF) {
            Dev->ChargeInhibited = FALSE;
            Dev->LastLimitStatus = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        Dev->LastLimitStatus = STATUS_DEVICE_NOT_READY;
        SwBatTrace(L"ChargeLimitStatus", (ULONG)Dev->LastLimitStatus);
        return Dev->LastLimitStatus;
    }

    Status = SwBatReadRegs(Dev->Charger, BQ_PWR_ON, &Pwr, 1);
    if (!NT_SUCCESS(Status)) {
        Dev->LastLimitStatus = Status;
        SwBatTrace(L"ChargeLimitStatus", (ULONG)Status);
        return Status;
    }

    SwBatChargeLimitBands(Dev->ChargeLimit, &BandMin, &BandMax);

    /* Never interfere with USB-C host VBUS boost (0x20 or 0x30 in REG01). */
    if (Dev->Usbc == UsbcHost || (Pwr & BQ_MODE_OTG) != 0) {
        if (Dev->ChargeLimit == SWBAT_CHARGE_LIMIT_OFF || SocPercent == 0 || SocPercent <= BandMin) {
            Dev->ChargeInhibited = FALSE;
        } else if (SocPercent >= BandMax) {
            Dev->ChargeInhibited = TRUE;
        }
        Dev->ChargeLimitInitialized = TRUE;
        Dev->LastLimitStatus = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    NextInhibited = Dev->ChargeInhibited;
    if (Dev->ChargeLimit == SWBAT_CHARGE_LIMIT_OFF || SocPercent == 0) {
        NextInhibited = FALSE;
    } else if (SocPercent <= BandMin) {
        NextInhibited = FALSE;
    } else if (SocPercent >= BandMax) {
        NextInhibited = TRUE;
    } else if (!Dev->ChargeLimitInitialized) {
        /* First sample inside (BandMin, BandMax) on startup: preserve an
         * existing non-suspend inhibit across warm reboot, else allow charging
         * up to BandMax just like Noble soc_reg_reached_max = false. */
        NextInhibited = !Dev->SuspendChargeDisabled &&
                        ((Pwr & BQ_MODE_MASK) == BQ_MODE_DISABLED);
    }

    DesiredMode = NextInhibited ? BQ_MODE_DISABLED : BQ_MODE_CHARGE;
    if ((Pwr & BQ_MODE_MASK) != DesiredMode) {
        Status = SwBatSetBqMode(Dev, DesiredMode);
        if (!NT_SUCCESS(Status)) {
            Dev->LastLimitStatus = Status;
            SwBatTrace(L"ChargeLimitStatus", (ULONG)Status);
            return Status;
        }
    }

    Dev->ChargeInhibited = NextInhibited;
    Dev->ChargeLimitInitialized = TRUE;
    Dev->SuspendChargeDisabled = FALSE;
    Dev->LastLimitStatus = STATUS_SUCCESS;
    SwBatTrace(L"ChargeInhibited", Dev->ChargeInhibited);
    SwBatTrace(L"ChargeLimitStatus", (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/* Read gauge SoC and enforce ChargeLimit immediately. Caller holds Dev->Lock. */
static NTSTATUS SwBatRefreshChargeLimit(PSWBAT_DEVICE Dev)
{
    USHORT Soc = 0;
    ULONG Percent;
    NTSTATUS Status;

    if (Dev->Gauge == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    Status = SwBatRead16(Dev->Gauge, GAUGE_REPSOC, &Soc);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Percent = Soc >> 8;
    if (Percent > 100) {
        Percent = 100;
    }
    return SwBatApplyChargeLimit(Dev, Percent);
}

/* Disable battery charging across suspend when a limit is active and not OTG,
 * matching Noble bq2419x_suspend without asserting BQ_HIZ. Caller holds Lock.
 */
static NTSTATUS SwBatSuspendChargeLimit(PSWBAT_DEVICE Dev)
{
    UCHAR Pwr = 0;
    NTSTATUS Status;

    if (Dev->ChargeLimit == SWBAT_CHARGE_LIMIT_OFF || Dev->Charger == NULL) {
        return STATUS_SUCCESS;
    }
    Status = SwBatReadRegs(Dev->Charger, BQ_PWR_ON, &Pwr, 1);
    if (!NT_SUCCESS(Status)) {
        SwBatTrace(L"ChargeSuspendStatus", (ULONG)Status);
        return Status;
    }
    if (Dev->Usbc == UsbcHost || (Pwr & BQ_MODE_OTG) != 0) {
        return STATUS_SUCCESS;
    }
    if ((Pwr & BQ_MODE_MASK) != BQ_MODE_DISABLED) {
        Status = SwBatSetBqMode(Dev, BQ_MODE_DISABLED);
        if (NT_SUCCESS(Status)) {
            Dev->SuspendChargeDisabled = TRUE;
        }
    } else {
        Dev->SuspendChargeDisabled = TRUE;
    }
    SwBatTrace(L"ChargeSuspendStatus", (ULONG)Status);
    return Status;
}

/* Populate live SWBAT_CHARGE_LIMIT_STATUS. Caller holds Dev->Lock. */
static NTSTATUS SwBatQueryChargeLimitStatus(PSWBAT_DEVICE Dev, SWBAT_CHARGE_LIMIT_STATUS *Out)
{
    USHORT Soc = 0, Vcell = 0;
    UCHAR In = 0, Pwr = 0, Sys = 0;
    ULONG Percent, BandMin = 0, BandMax = 100;
    ULONG VbusStat, ChrgStat, Flags = 0;
    NTSTATUS Status, ApplyStatus;

    if (Dev->Stopping || Dev->Gauge == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    Status = SwBatRead16(Dev->Gauge, GAUGE_REPSOC, &Soc);
    if (NT_SUCCESS(Status)) {
        Status = SwBatRead16(Dev->Gauge, GAUGE_VCELL, &Vcell);
    }
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Percent = Soc >> 8;
    if (Percent > 100) {
        Percent = 100;
    }

    ApplyStatus = SwBatApplyChargeLimit(Dev, Percent);
    if (Dev->Charger != NULL) {
        Status = SwBatReadRegs(Dev->Charger, BQ_INPUT_SRC, &In, 1);
        if (NT_SUCCESS(Status)) {
            Status = SwBatReadRegs(Dev->Charger, BQ_PWR_ON, &Pwr, 1);
        }
        if (NT_SUCCESS(Status)) {
            Status = SwBatReadRegs(Dev->Charger, CHARGER_SYSSTAT, &Sys, 1);
        }
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        Flags |= SWBAT_CHARGE_FLAG_CHARGER_READY;
    } else if (!NT_SUCCESS(ApplyStatus)) {
        return ApplyStatus;
    }

    SwBatChargeLimitBands(Dev->ChargeLimit, &BandMin, &BandMax);
    VbusStat = (Sys >> 6) & 3u;
    ChrgStat = (Sys >> 4) & 3u;
    if (Dev->ChargeInhibited) {
        Flags |= SWBAT_CHARGE_FLAG_INHIBITED;
    }
    if ((VbusStat == 1u || VbusStat == 2u) && (Sys & 0x04u) != 0) {
        Flags |= SWBAT_CHARGE_FLAG_EXTERNAL_ONLINE;
        if (ChrgStat == 1u || ChrgStat == 2u) {
            Flags |= SWBAT_CHARGE_FLAG_CHARGING;
        }
    }
    if (Dev->Usbc == UsbcHost || (Pwr & BQ_MODE_OTG) != 0) {
        Flags |= SWBAT_CHARGE_FLAG_OTG_ACTIVE;
    }
    if ((In & BQ_HIZ) != 0) {
        Flags |= SWBAT_CHARGE_FLAG_HIZ_ACTIVE;
    }
    if (Dev->SuspendChargeDisabled) {
        Flags |= SWBAT_CHARGE_FLAG_SUSPEND_HOLD;
    }

    RtlZeroMemory(Out, sizeof(*Out));
    Out->Version = SWBAT_CHARGE_LIMIT_STATUS_VERSION;
    Out->LimitCenter = Dev->ChargeLimit;
    Out->BandMin = BandMin;
    Out->BandMax = BandMax;
    Out->CapacityPercent = Percent;
    Out->VoltageMv = ((ULONG)(Vcell >> 3) * 625u) / 1000u;
    Out->Reg00InputSrc = In;
    Out->Reg01PwrOn = Pwr;
    Out->Reg08SysStat = Sys;
    Out->LastApplyStatus = (unsigned int)Dev->LastLimitStatus;
    Out->Flags = Flags;
    return ApplyStatus;
}

static NTSTATUS SwBatSetChargeLimitValue(PSWBAT_DEVICE Dev, ULONG NewLimit, BOOLEAN *OutNotify)
{
    USHORT Soc = 0;
    ULONG Percent, OldLimit;
    BOOLEAN OldInhibited, OldInit;
    NTSTATUS Status;

    if (OutNotify != NULL) {
        *OutNotify = FALSE;
    }
    if (!SwBatChargeLimitValid(NewLimit)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Dev->Stopping || Dev->Gauge == NULL || Dev->Charger == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    Status = SwBatRead16(Dev->Gauge, GAUGE_REPSOC, &Soc);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Percent = Soc >> 8;
    if (Percent > 100) {
        Percent = 100;
    }

    OldLimit = Dev->ChargeLimit;
    OldInhibited = Dev->ChargeInhibited;
    OldInit = Dev->ChargeLimitInitialized;

    Dev->ChargeLimit = NewLimit;
    if (NewLimit != OldLimit) {
        Dev->ChargeInhibited = FALSE;
        Dev->ChargeLimitInitialized = TRUE;
    }

    Status = SwBatApplyChargeLimit(Dev, Percent);
    if (!NT_SUCCESS(Status)) {
        Dev->ChargeLimit = OldLimit;
        Dev->ChargeInhibited = OldInhibited;
        Dev->ChargeLimitInitialized = OldInit;
        return Status;
    }

    if (OutNotify != NULL && (NewLimit != OldLimit || Dev->ChargeInhibited != OldInhibited)) {
        *OutNotify = TRUE;
    }
    return STATUS_SUCCESS;
}

#endif
