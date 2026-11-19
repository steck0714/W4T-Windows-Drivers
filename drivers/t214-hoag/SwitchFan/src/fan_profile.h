/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2021, CTCaer
 * Continuous thermal governor math adapted from switch-l4t-kernel-nvidia
 * 76e6d48970b451c242c20f298b8d63027836bb0b, continuous_thermal_gov.c.
 * Profile data: same-machine Noble device tree captured 2026-11-14.
 * Windows supplies max(TMP451 remote, local), not Linux's skin estimator;
 * retain independent sensor-loss/overtemperature protection.
 */
#ifndef SWITCHFAN_PROFILE_H
#define SWITCHFAN_PROFILE_H
#include <stdint.h>
#include "fan_profile_ioctl.h"
struct sw_fan_curve { int32_t temp[10], hyst[10]; uint32_t pwm[10]; };
static const struct sw_fan_curve sw_fan_curves[SwFanProfileCount] = {
    {{0,36000,40000,43000,53000,58000,70000,80000,90000,100000},
     {0,0,3900,2900,0,0,0,0,0,0}, {0,0,51,51,153,255,255,255,255,255}},
    {{0,36000,40000,43000,48000,53000,58000,60000,90000,100000},
     {0,0,3900,2900,0,4900,0,0,0,0}, {0,0,51,51,102,153,204,255,255,255}},
    {{0,32000,37000,40000,45000,55000,70000,80000,90000,100000},
     {0,0,0,0,0,0,0,0,0,0}, {0,0,51,51,153,255,255,255,255,255}}
};
struct sw_fan_filter { int32_t sta, lta; uint32_t initialized, on; };
static inline uint32_t sw_fan_curve_pwm(uint32_t mode, int32_t temp, uint32_t *on)
{
    if (mode >= SwFanProfileCount || !on || temp < 0 || temp > 127000) return 255;
    const struct sw_fan_curve *c=&sw_fan_curves[mode];
    if (!*on) { if (temp>c->temp[1]) *on=1; }
    else if (temp<c->temp[1]-c->hyst[1]) *on=0;
    if (!*on) return 0;
    uint32_t i;
    for (i=1;i<10;++i) if (temp<c->temp[i]-c->hyst[i]) break;
    if (i>=10) i=9;
    const int64_t slope=(int64_t)(c->pwm[i]-c->pwm[i-1u])*10000/
                         (c->temp[i]-c->temp[i-1u]);
    const int64_t pwm=(slope*(temp-c->temp[i])+(int64_t)c->pwm[i]*10000)/10000;
    return pwm<0 ? 0u : (pwm>255 ? 255u : (uint32_t)pwm);
}
static inline uint32_t sw_fan_profile_sample(struct sw_fan_filter *s,
                                            uint32_t mode, int32_t raw, int valid)
{
    if (!s || !valid || mode>=SwFanProfileCount || raw<0 || raw>127000) return 255;
    /* Raw emergency temperature bypasses the smoothing delay. */
    if (raw>=80000) { s->sta=s->lta=raw; s->initialized=s->on=1; return 255; }
    if (!s->initialized) { s->sta=s->lta=raw; s->initialized=1; }
    s->sta += 2*(raw-s->sta)/10;
    int32_t delta=s->sta-s->lta;
    const int32_t width=delta>0 ? 5000 : 15000;
    const int32_t magnitude=delta<0 ? -delta : delta;
    int64_t gain=1000;
    /* Clamp before exponentiation: the upstream final clamp is identical
     * above width, without risking integer overflow on abrupt sensor changes. */
    if (magnitude<width) {
        gain=990;
        for (unsigned i=0;i<7;++i) gain=gain*magnitude/width;
        gain+=10;
    }
    s->lta+=(int32_t)(gain*delta/1000);
    return sw_fan_curve_pwm(mode,s->lta,&s->on);
}
static inline uint32_t sw_fan_pwm_duty(uint32_t pwm)
{ return pwm>=255 ? 236u : (pwm*236u+254u)/255u; }
#endif
