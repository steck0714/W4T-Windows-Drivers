/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "../src/gauge_telemetry.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);abort(); } } while (0)
int main(void)
{
    SWBAT_TELEMETRY_STATUS s={0};
    CHECK(sizeof(s)==40);
    CHECK(sw_bat_convert_telemetry(0xac,0x7fff,51200,6400,&s));
    CHECK(s.VoltageUv==4000000 && s.CurrentUa==1000000 && s.PowerUw==4000000);
    CHECK(sw_bat_convert_telemetry(0xac,0x7fff,51200,(uint16_t)-6400,&s));
    CHECK(s.CurrentUa==-1000000 && s.PowerUw==-4000000);
    CHECK(sw_bat_convert_telemetry(0xac,0x7fff,51200,0,&s));
    CHECK(s.Flags==1 && !s.CurrentUa && !s.PowerUw);
    CHECK(sw_bat_convert_telemetry(0xac,0x7fff,51200,0x8000,&s));
    CHECK(s.CurrentUa==-5120000 && s.PowerUw==-20480000);
    CHECK(sw_bat_convert_telemetry(0xac,0x7fff,51200,0x7fff,&s));
    CHECK(s.CurrentUa==5119843 && s.PowerUw==20479372);
    CHECK(!sw_bat_convert_telemetry(0xad,0x7fff,51200,6400,&s) && !s.Flags);
    CHECK(!sw_bat_convert_telemetry(0xac,0x4000,51200,6400,&s) && !s.Flags);
    CHECK(!sw_bat_convert_telemetry(0xac,0x7fff,0,6400,&s));
    CHECK(!sw_bat_convert_telemetry(0xac,0x7fff,65535,6400,&s));
    puts("PASS: signed current and power, 2x board gain, zero, extrema and unsupported calibration (host control)");
}
