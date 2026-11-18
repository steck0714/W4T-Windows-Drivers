/* W4T-CREDIT-BEGIN
 * Origin : ruje0504-2/Windows-for-Switch-Drivers, Lite (t214/Hoag) line; shared implementations come from
 *          hayato41200/Windows-for-Switch-Drivers (see drivers/t214-hoag/README.md).
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : CTCaer/hekate (GPL-2.0): ambient light sensor.
 * Credit block added by W4T (2026-11-18); full table in CREDITS.md.
 * W4T-CREDIT-END */
/* SwitchCenter sensor status shared by the SPI and SIO transports. */
#ifndef SWITCH_SENSOR_STATUS_H
#define SWITCH_SENSOR_STATUS_H

#define IOCTL_SWSENS_GET  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_READ_ACCESS)   /* 0x00226080 */
#define SENS_ALS_VALID    1u
#define SENS_IMU_VALID    2u

typedef struct _SWSENS_STATUS {
    ULONG Version;          /* 1 */
    ULONG Flags;            /* SENS_ALS_VALID, SENS_IMU_VALID */
    ULONG LuxMilli;         /* lux x 1000 */
    ULONG Data0;            /* visible + IR */
    ULONG Data1;            /* IR */
    ULONG Gain;             /* 1, 2, 64, 128 */
    LONG AccelX;            /* mg, chip axes */
    LONG AccelY;
    LONG AccelZ;
    ULONG ImuId;            /* WHO_AM_I */
    ULONG Samples;
} SWSENS_STATUS;

static inline VOID SwSensReturnStatus(WDFREQUEST Request, ULONG Code, WDFSPINLOCK Lock, const SWSENS_STATUS *Snapshot)
{
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR Info = 0;

    if (Code == IOCTL_SWSENS_GET) {
        SWSENS_STATUS *Out;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID *)&Out, NULL);
        if (NT_SUCCESS(Status)) {
            WdfSpinLockAcquire(Lock);
            *Out = *Snapshot;
            WdfSpinLockRelease(Lock);
            Out->Version = 1;
            Info = sizeof(*Out);
        }
    }
    WdfRequestCompleteWithInformation(Request, Status, Info);
}
#endif
