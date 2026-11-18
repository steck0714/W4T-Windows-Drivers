/* W4T-CREDIT-BEGIN
 * Origin : ruje0504-2/Windows-for-Switch-Drivers, Lite (t214/Hoag) line; shared implementations come from
 *          hayato41200/Windows-for-Switch-Drivers (see drivers/t214-hoag/README.md).
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : CTCaer/hekate (GPL-2.0): MAX17050 / BQ24193 / BM92T36 registers and sequences; structure after Microsoft's SimBatt sample (as stated in the file).
 * Credit block added by W4T (2026-11-18); full table in CREDITS.md.
 * W4T-CREDIT-END */
/* BQ24193 input-current codes. Lite limits and port reserve come from
 * Switchroot's Vali power tree and bm92txx.c. All arguments use mV/mA. */
#ifndef SWITCH_USB_PD_H
#define SWITCH_USB_PD_H

enum {
    SwBatErista,
    SwBatLite
};

static inline unsigned SwBatCurrentCode(unsigned board, unsigned mv, unsigned advertised, unsigned operating,
                                        unsigned *actual)
{
    static const unsigned bq[] = {100, 150, 500, 900, 1200, 1500, 2000, 3000};
    unsigned limit = 500;
    if (board == SwBatLite && mv && advertised) {
        unsigned reserve = mv > 5000 ? 4500000u / mv : 2500000u / mv;
        unsigned budget = advertised;
        if (operating && operating < budget)
            budget = operating;
        if (mv == 5000)
            limit = 2000;
        else if (mv == 9000)
            limit = 1500;
        else if (mv == 12000)
            limit = 1200;
        else if (mv == 15000)
            limit = 900;
        if (!operating || mv > 15000)
            limit = 500;
        budget = budget > reserve ? budget - reserve : 0;
        if (budget < limit)
            limit = budget;
    } else if (board == SwBatErista && mv) {
        if (mv <= 9500)
            limit = 2000;
        else if (mv <= 12500)
            limit = 1500;
        else if (mv <= 15500)
            limit = 1200;
        if (advertised && advertised < limit)
            limit = advertised;
    }
    if (board == SwBatLite && limit < bq[0]) {
        *actual = 0;
        return 0x80; /* HIZ: the port reserve consumes the input budget. */
    }
    unsigned code = (sizeof(bq) / sizeof(bq[0])) - 1;
    while (code && bq[code] > limit)
        --code;
    *actual = bq[code];
    return code;
}
#endif
