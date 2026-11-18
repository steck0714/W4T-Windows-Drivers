/* W4T-CREDIT-BEGIN
 * Origin : hayato41200/Windows-for-Switch-Drivers, Erista (t210/Icosa) driver set, which matched an older
 *          ruje0504-2/Windows-for-Switch-Drivers commit (docs/HAYATO-DRIVER-AUDIT-ja.md); later edits are in CREDITS.md.
 * Authors: hayato41200, ruje0504-2 (and their contributors). The upstream file has no author or
 *          copyright line, so per-file authorship is not recorded here.
 * License: GPL-2.0, from the LICENSE of this directory (the file has no SPDX tag of its own).
 * Refs   : CTCaer/hekate (GPL-2.0): MAX77620 registers.
 * Credit block added by W4T (2026-11-18); full table in CREDITS.md.
 * W4T-CREDIT-END */
/*
 * Power-button hold logic for switchpwrbtn (pure C, no kernel types, host-testable).
 *
 * Ported from the behaviour of windows-switch's swhid (power button: short press = power
 * action, hold 4 s = Ctrl+Alt+Del so Windows shows its security screen with the power menu),
 * but expressed for winx's SWOA0601 driver, which polls MAX77620 ONOFFSTAT.EN0 every poll.
 *
 * hold_sas_ms == 0  : legacy 1.0.0.1 behaviour. Every change of the button is reported at once
 *                     as System Power Down press / release.
 * hold_sas_ms  > 0  : the power action is reported on RELEASE (press + release back to back) if
 *                     the button was held shorter than hold_sas_ms. If it stays down for
 *                     hold_sas_ms, a Ctrl+Alt+Del key press is emitted once (released
 *                     SWPWR_SAS_DOWN_MS later, or on release if that comes first) and no power
 *                     action is reported for that press.
 *
 * A button already down when the driver starts is ignored until released.
 */
#ifndef SWPWR_HOLD_SAS_H
#define SWPWR_HOLD_SAS_H

#define SWPWR_SAS_DOWN_MS 100u   /* Ctrl+Alt+Del held for 100 ms before release (as swhid) */

enum swpwr_action {
    SWPWR_ACT_NONE = 0,
    SWPWR_ACT_POWER_DOWN,        /* System Power Down: pressed  */
    SWPWR_ACT_POWER_UP,          /* System Power Down: released */
    SWPWR_ACT_SAS_DOWN,          /* keyboard report: Left Ctrl + Left Alt + Delete */
    SWPWR_ACT_SAS_UP             /* keyboard report: all keys released */
};

struct swpwr_hold {
    unsigned held_ms;            /* how long the current press has lasted */
    unsigned sas_ms;             /* time since SAS_DOWN was sent */
    unsigned char held;          /* button level as of the previous step */
    unsigned char fired;         /* SAS_DOWN sent for this press */
    unsigned char sas_up_sent;   /* SAS_UP sent for this press */
    unsigned char ignore;        /* was already down at start: swallow until release */
};

static void swpwr_hold_init(struct swpwr_hold *s, int held_at_start)
{
    s->held_ms = 0; s->sas_ms = 0; s->fired = 0; s->sas_up_sent = 0;
    s->held = held_at_start ? 1 : 0;
    s->ignore = held_at_start ? 1 : 0;
}

/*
 * One poll. held = EN0 now; released_between = ONOFFIRQ saw a release edge while the level
 * reads "up" (press shorter than one poll). Writes up to 2 actions to acts[], returns the count.
 */
static int swpwr_hold_step(struct swpwr_hold *s, unsigned hold_sas_ms, unsigned poll_ms,
                           int held, int released_between, enum swpwr_action acts[2])
{
    int n = 0;
    held = held ? 1 : 0;

    if (hold_sas_ms == 0) {                       /* legacy: report every edge now */
        if (held != s->held) {
            s->held = (unsigned char)held;
            acts[n++] = held ? SWPWR_ACT_POWER_DOWN : SWPWR_ACT_POWER_UP;
        } else if (!held && released_between) {
            acts[n++] = SWPWR_ACT_POWER_DOWN;
            acts[n++] = SWPWR_ACT_POWER_UP;
        }
        s->ignore = 0;
        return n;
    }

    if (held) {
        if (!s->held) {                           /* rising edge */
            s->held_ms = 0; s->sas_ms = 0; s->fired = 0; s->sas_up_sent = 0; s->ignore = 0;
            s->held = 1;
        }
        if (s->ignore) return 0;
        s->held_ms += poll_ms;
        if (!s->fired && s->held_ms >= hold_sas_ms) {
            s->fired = 1; s->sas_ms = 0;
            acts[n++] = SWPWR_ACT_SAS_DOWN;
        } else if (s->fired && !s->sas_up_sent) {
            s->sas_ms += poll_ms;
            if (s->sas_ms >= SWPWR_SAS_DOWN_MS) {
                s->sas_up_sent = 1;
                acts[n++] = SWPWR_ACT_SAS_UP;
            }
        }
        return n;
    }

    if (s->held) {                                /* falling edge */
        s->held = 0;
        if (s->ignore) { s->ignore = 0; return 0; }
        if (s->fired) {
            if (!s->sas_up_sent) acts[n++] = SWPWR_ACT_SAS_UP;
        } else {
            acts[n++] = SWPWR_ACT_POWER_DOWN;
            acts[n++] = SWPWR_ACT_POWER_UP;
        }
        s->held_ms = 0; s->fired = 0; s->sas_up_sent = 0;
        return n;
    }
    if (released_between) {                       /* pressed and released between two polls */
        acts[n++] = SWPWR_ACT_POWER_DOWN;
        acts[n++] = SWPWR_ACT_POWER_UP;
    }
    return n;
}

#endif
