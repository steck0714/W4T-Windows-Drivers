/* SPDX-License-Identifier: MIT
 * MAX77620 SD0 VDD_CORE: 600 mV + 12.5 mV/code, REG16[5:0].
 * Keep the firmware's rail floor for the other, still fixed-rate core clients.
 * EMC changes only its own additional vote. Caller serializes these requests.
 */
#ifndef SWITCH_CORE_VOLTAGE_CONTROL_H
#define SWITCH_CORE_VOLTAGE_CONTROL_H
#include <stdint.h>
#include <stdbool.h>
struct sw_core_voltage {
    uint32_t boot_uv, vote_uv, applied_uv;
};
struct sw_core_io {
    void *context;
    bool (*read)(void *, uint8_t *);
    bool (*write)(void *, uint8_t);
    void (*settle)(void *);
};
static uint32_t sw_core_uv(uint8_t value)
{ return 600000u + (value & 63u) * 12500u; }
static bool sw_core_voltage_vote(struct sw_core_voltage *s,
                                 const struct sw_core_io *io, bool set,
                                 uint32_t minimum_uv)
{
    uint8_t before = 0, target, after = 0;
    uint32_t required, old_vote = s->vote_uv;
    if (set && minimum_uv && (minimum_uv < 600000u || minimum_uv > 1050000u)) return false;
    if (!io->read(io->context, &before)) return false;
    s->applied_uv = sw_core_uv(before);
    if (!s->boot_uv) {
        if (s->applied_uv > 1050000u) return false;
        s->boot_uv = s->applied_uv;
    }
    if (!set) return true;
    required = minimum_uv > s->boot_uv ? minimum_uv : s->boot_uv;
    target = (uint8_t)((before & 0xc0u) | ((required - 600000u + 12499u) / 12500u));
    /* A failed write/readback may still have changed the rail. Keep the
     * higher vote on error; EMC must not authorize a clock increase. */
    s->vote_uv = minimum_uv > old_vote ? minimum_uv : old_vote;
    if (target != before && !io->write(io->context, target)) {
        s->applied_uv = io->read(io->context, &after) ? sw_core_uv(after) : 0;
        return false;
    }
    if (!io->read(io->context, &after)) { s->applied_uv = 0; return false; }
    s->applied_uv = sw_core_uv(after);
    if (after != target) return false;
    io->settle(io->context);
    s->vote_uv = minimum_uv;
    return true;
}
#endif
