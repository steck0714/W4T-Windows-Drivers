/* SPDX-License-Identifier: MIT
 * MAX17050 register conversion, based on its published units and Switch board
 * calibration in Hekate 8e1392df4569ac501a115867056b13c361429e9e:
 * bdk/power/max17050.c BOARD_RSENSE_MOHM=5, BOARD_CGAIN=2 (0x7fff).
 * Current is signed 156.25 uA/LSB AFTER that gain; using bare 5 mOhm would
 * report twice the real current. Unknown gauges/calibration are rejected.
 * We read calibration only; never reprogram it to obtain a measurement.
 */
#ifndef SWITCHBATTERY_GAUGE_TELEMETRY_H
#define SWITCHBATTERY_GAUGE_TELEMETRY_H
#include <stdint.h>
#include "telemetry_ioctl.h"
static inline int sw_bat_convert_telemetry(uint16_t gauge, uint16_t gain,
    uint16_t vcell, uint16_t current, SWBAT_TELEMETRY_STATUS *out)
{
    if (!out) return 0;
    out->Flags=0;out->VoltageUv=0;out->CurrentUa=out->PowerUw=0;
    out->GaugeId=gauge;out->Cgain=gain;out->RawCurrent=current;
    if (gauge!=0x00acu || gain!=0x7fffu) return 0;
    uint32_t uv=(uint32_t)(vcell>>3)*625u;
    if (uv<2500000u || uv>5000000u) return 0;
    int32_t raw=current<0x8000u ? (int32_t)current : (int32_t)current-65536;
    int64_t ua=(int64_t)raw*625/4;
    int64_t uw=(int64_t)uv*ua/1000000;
    if (uw<INT32_MIN || uw>INT32_MAX) return 0;
    out->VoltageUv=uv;out->CurrentUa=(int)ua;out->PowerUw=(int)uw;
    out->Flags=SWBAT_TELEMETRY_VALID;
    return 1;
}
#endif
