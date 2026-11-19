/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/t214/emc_qos_budget.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); abort(); } } while (0)
int main(void)
{
    const uint32_t rates[]={204000,1331200,1600000};
    const uint32_t rings[]={53,232,274};
    struct sw_emc_qos_budget b, zero={0};
    CHECK(sw_emc_qos_budget_plan(rates,3,SW_EMC_PTSA_POLICY_STANDARD,312000,0,0,12,0,&b));
    CHECK(b.count==3 && b.display==16);
    for (uint32_t i=0;i<b.count;++i) {
        const struct sw_emc_qos_point *p=&b.points[i];
        CHECK(p->khz==rates[i] && p->ring==rings[i]);
        CHECK((uint64_t)b.display*p->khz*640u >= (uint64_t)p->clock.grant*312000u*11u);
        CHECK(b.display*4u<p->clock.grant && p->ring*2u<p->clock.grant);
        /* A lower clock's grant stays sufficient through an upshift; lowering
         * grant before a downshift also preserves the display's catch-up BW. */
        for (uint32_t j=i;j<b.count;++j)
            CHECK((uint64_t)b.display*b.points[j].khz*640u >=
                  (uint64_t)p->clock.grant*312000u*11u);
    }
    /* Regression: the previous full-register-maximum reservation is too large
     * if firmware supplies the actual 204 MHz clock terms. */
    CHECK(216u*4u>=b.points[0].clock.grant);
    CHECK(571u*2u>=b.points[0].clock.grant);
    CHECK(!sw_emc_qos_budget_plan(rates,3,SW_EMC_PTSA_POLICY_STANDARD,312000,0,4095,12,0,&b));
    CHECK(!memcmp(&b,&zero,sizeof(b)));
    const uint32_t unordered[]={204000,1600000,1331200};
    memset(&b,0xa5,sizeof(b));
    CHECK(!sw_emc_qos_budget_plan(unordered,3,SW_EMC_PTSA_POLICY_STANDARD,312000,0,0,12,0,&b));
    CHECK(!memcmp(&b,&zero,sizeof(b)));
    CHECK(!sw_emc_qos_budget_plan(rates,0,SW_EMC_PTSA_POLICY_STANDARD,312000,0,0,12,0,&b));
    CHECK(!sw_emc_qos_budget_plan(rates,33,SW_EMC_PTSA_POLICY_STANDARD,312000,0,0,12,0,&b));
    CHECK(!sw_emc_qos_budget_plan(rates,3,(enum sw_emc_ptsa_policy)2,312000,0,0,12,0,&b));
    CHECK(!sw_emc_qos_budget_plan(rates,3,SW_EMC_PTSA_POLICY_STANDARD,UINT32_MAX,0,0,12,0,&b));
    puts("PASS: real-rate LPDDR4 budget and over-reservation rejection; planning only, no BPMP ownership or hardware claim");
    return 0;
}
