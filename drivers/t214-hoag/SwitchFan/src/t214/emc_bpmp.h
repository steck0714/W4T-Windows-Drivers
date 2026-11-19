/* SPDX-License-Identifier: GPL-2.0-only
 * Channel token protocol: Copyright (c) 2013-2017 NVIDIA CORPORATION.
 * Adapted from drivers/firmware/tegra/mail_t210.c and nv_bpmp.c,
 * CTCaer/switch-l4t-kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b.
 * Windows binding must own one of T210 threaded channels 4..7 through ACPI,
 * serialize calls at PASSIVE_LEVEL, and drain calls before D0Exit/unmapping.
 * Do not reuse TF-A/UEFI's per-CPU channel 0 with interrupts enabled.
 */
#ifndef SWITCHFAN_EMC_BPMP_H
#define SWITCHFAN_EMC_BPMP_H
#include <stdint.h>
#include <string.h>

#define SW_EMC_BPMP_PAYLOAD 120u
enum { SwBpmpInvalid = -101, SwBpmpBusy = -102, SwBpmpTimeout = -103,
       SwBpmpPoisoned = -104 };

struct sw_emc_mail_ops {
    uint32_t (*read_token)(void *);
    void (*clear_token)(void *, uint32_t);
    void (*read_mail)(void *, uint32_t offset, void *, uint32_t size);
    void (*write_mail)(void *, uint32_t offset, const void *, uint32_t size);
    void (*barrier)(void *);
    void (*ring)(void *);
    uint64_t (*time_us)(void *);
    /* Kernel binding: a bounded, non-alertable PASSIVE_LEVEL delay; do not
     * hold a spinlock or disable interrupts through the timeout interval. */
    void (*wait)(void *);
};
struct sw_emc_mailbox {
    struct sw_emc_mail_ops ops;
    void *context;
    uint32_t channel;
    int owned, poisoned;
};

static inline int sw_emc_mailbox_init(struct sw_emc_mailbox *m,
                                     struct sw_emc_mail_ops ops, void *context,
                                     uint32_t channel)
{
    memset(m, 0, sizeof(*m));
    if (channel < 4u || channel > 7u || !ops.read_token || !ops.clear_token ||
        !ops.read_mail || !ops.write_mail || !ops.barrier || !ops.ring ||
        !ops.time_us || !ops.wait) return SwBpmpInvalid;
    m->ops = ops;
    m->context = context;
    m->channel = channel;
    /* The caller must establish OS resource ownership before calling init.
     * A busy/acked channel is not ours to force-reset or steal. */
    if (((ops.read_token(context) >> (channel * 2u)) & 3u) != 2u)
        return SwBpmpBusy;
    m->owned = 1;
    return 0;
}

static inline int sw_emc_mailbox_message(void *context, uint32_t mrq,
                                         const void *input, uint32_t in_size,
                                         void *output, uint32_t out_size)
{
    struct sw_emc_mailbox *m = context;
    uint32_t header[2] = {mrq, 1u}; /* DO_ACK, no response IRQ requested */
    uint8_t payload[SW_EMC_BPMP_PAYLOAD] = {0};
    uint32_t shift;
    uint64_t start;
    int32_t result;
    if (!m || !m->owned || in_size > sizeof(payload) || out_size > sizeof(payload) ||
        (in_size && !input) || (out_size && !output)) return SwBpmpInvalid;
    if (m->poisoned) return SwBpmpPoisoned;
    shift = m->channel * 2u;
    if (((m->ops.read_token(m->context) >> shift) & 3u) != 2u) return SwBpmpBusy;
    if (in_size) memcpy(payload, input, in_size);
    m->ops.write_mail(m->context, 0, header, sizeof(header));
    m->ops.write_mail(m->context, 8, payload, sizeof(payload));
    m->ops.barrier(m->context);
    m->ops.clear_token(m->context, 3u << shift); /* FREE -> SIGNALED */
    m->ops.barrier(m->context);
    m->ops.ring(m->context);
    start = m->ops.time_us(m->context);
    while (((m->ops.read_token(m->context) >> shift) & 3u) != 3u) {
        if (m->ops.time_us(m->context) - start >= 500000u) {
            /* Firmware may still execute this request. Never overwrite it,
             * free its token or drop a rail vote on the basis of a timeout. */
            m->poisoned = 1;
            return SwBpmpTimeout;
        }
        m->ops.wait(m->context);
    }
    m->ops.barrier(m->context);
    m->ops.read_mail(m->context, 0, &result, sizeof(result));
    if (!result && out_size) m->ops.read_mail(m->context, 8, output, out_size);
    m->ops.barrier(m->context);
    m->ops.clear_token(m->context, 1u << shift); /* ACKED -> FREE */
    return result;
}
#endif
