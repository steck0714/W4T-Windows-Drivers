/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/emc_handoff.h"

int main(void)
{
    LITE_EMC_HANDOFF h = {LITE_EMC_HANDOFF_MAGIC, 1, sizeof(h),
        0x2120, 0x83, 7, 0x40028fc0, 1659, 1675};
    assert(LiteEmcHandoffValid(&h));
    assert(LiteEmcMailRegion(h.MailAddress) == LITE_EMC_REGION_IRAM);
    h.MailAddress = LITE_EMC_IRAM_BASE;
    assert(LiteEmcHandoffValid(&h));
    h.MailAddress = LITE_EMC_IRAM_BASE + LITE_EMC_IRAM_SIZE - 128u;
    assert(LiteEmcHandoffValid(&h));
    h.MailAddress += 4u;
    assert(!LiteEmcHandoffValid(&h));
    h.MailAddress = LITE_EMC_MAIL_BASE + LITE_EMC_MAIL_SIZE - 128u;
    assert(LiteEmcHandoffValid(&h));
    assert(LiteEmcMailRegion(h.MailAddress) == LITE_EMC_REGION_DRAM);
    h.MailAddress += 4u;
    assert(!LiteEmcHandoffValid(&h));
    h.MailAddress = LITE_EMC_MAIL_BASE + 1u;
    assert(!LiteEmcHandoffValid(&h));
    h.MailAddress = 0xfffffffcu;
    assert(!LiteEmcHandoffValid(&h));
    h.MailAddress = 0x40028fc0;
    h.Channel = 0;
    assert(!LiteEmcHandoffValid(&h));
    h.Channel = 7;
    h.Magic = 0;
    assert(!LiteEmcHandoffValid(&h));
    puts("PASS: IRAM/DRAM EMC handoff, bounds, alignment and channel ownership (host only)");
    return 0;
}
