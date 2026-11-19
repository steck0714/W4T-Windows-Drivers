/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include "../src/fan_profile.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);abort(); } } while (0)
int main(void)
{
    for (uint32_t mode=0;mode<3;++mode) {
        uint32_t on=0;
        CHECK(sw_fan_curve_pwm(mode,30000,&on)==0);
        CHECK(sw_fan_curve_pwm(mode,80000,&on)==255);
        for (int32_t t=0;t<=127000;t+=100) CHECK(sw_fan_curve_pwm(mode,t,&on)<=255);
        struct sw_fan_filter f={0};
        CHECK(sw_fan_profile_sample(&f,mode,30000,1)==0);
        for (unsigned i=0;i<1000;++i) CHECK(sw_fan_profile_sample(&f,mode,60000,1)<=255);
        CHECK(f.lta>59000);
        CHECK(sw_fan_profile_sample(&f,mode,80000,1)==255 && f.lta==80000);
        CHECK(sw_fan_profile_sample(&f,mode,30000,0)==255);
    }
    uint32_t on=1;
    CHECK(sw_fan_curve_pwm(SwFanProfileHandheld,48000,&on)==102);
    CHECK(sw_fan_curve_pwm(SwFanProfileConsole,48000,&on)==102);
    CHECK(sw_fan_curve_pwm(SwFanProfileCool,45000,&on)==153);
    CHECK(sw_fan_curve_pwm(SwFanProfileHandheld,60000,&on)==255);
    CHECK(sw_fan_curve_pwm(3,40000,&on)==255);
    CHECK(sw_fan_pwm_duty(0)==0 && sw_fan_pwm_duty(255)==236);
    CHECK(sw_fan_pwm_duty(51)==48 && sw_fan_pwm_duty(153)==142);
    CHECK(sizeof(SWFAN_PROFILE_STATUS)==28 && sizeof(SWFAN_PROFILE_REQUEST)==8);
    puts("PASS: Noble fan tables, continuous filter bounds, emergency bypass and PWM mapping (host control)");
}
