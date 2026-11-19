/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2014-2019 NVIDIA CORPORATION.
 * Copyright (c) 2021-2023 CTCaer.
 * LPDDR4 PTSA clock terms adapted from tegra21x_la.c:t21x_init_ptsa,
 * tegra_la_get_t21x_specific and la_priv.h:__fraction2dda_fp at
 * switch-l4t-kernel-nvidia 76e6d48970b451c242c20f298b8d63027836bb0b.
 *
 * This calculation is not an MMIO initializer or a trained timing table.
 * The caller must validate the T210B01 identity and actual BPMP rate list,
 * establish all participating PTSA clients, and obey timing-update ordering.
 * In particular, a zero grant does NOT constitute a usable QoS configuration.
 */
#ifndef SW_T214_EMC_PTSA_H
#define SW_T214_EMC_PTSA_H
#include <stdint.h>

struct sw_emc_ptsa_clock {
    uint32_t grant, cpu_read, cpu_write;
};
/* Native B012500MHz boot (2026-11-15): MTC seeds these three clock words,
 * while DIS/RING/client bounds retain reset state. Hekate8e1392df's
 * la_scale_regs_mc_addr_table includes these clock words and client latency
 * registers; non-clock state must be validated separately.
 * This is a narrow observed seed, NOT an accepted live QoS configuration.
 * The bootstrap below must still prove EVERY other client is reset. */
static inline int sw_emc_ptsa_mtc_seed(uint32_t actual, uint32_t maximum,
    uint32_t grant, uint32_t read, uint32_t write)
{
    return actual==2500000u && maximum==2500000u &&
        grant==6143u && read==277u && write==31u;
}
enum sw_emc_ptsa_policy {
    SW_EMC_PTSA_POLICY_STANDARD=0,
    SW_EMC_PTSA_POLICY_REWORK=1
};

static inline uint32_t sw_emc_ptsa_dda(uint32_t fraction_fpa)
{
    /* Exact 12-bit ceiling of the upstream bit-serial FPA conversion. */
    uint64_t dda=((uint64_t)fraction_fpa*4096u+9999u)/10000u;
    return dda>0xfffu ? 0xfffu : (uint32_t)dda;
}

static inline int sw_emc_ptsa_clock_values(uint32_t actual_khz,
    uint32_t trained_max_khz, enum sw_emc_ptsa_policy policy,
    struct sw_emc_ptsa_clock *out)
{
    uint32_t lo=176u, mhz, grant_fpa, integer, read_bw, write_bw;
    uint64_t adjusted;
    if (!out) return 0;
    *out=(struct sw_emc_ptsa_clock){0};
    if (policy!=SW_EMC_PTSA_POLICY_STANDARD && policy!=SW_EMC_PTSA_POLICY_REWORK) return 0;
    if (actual_khz<25000u || actual_khz>trained_max_khz ||
        trained_max_khz>3200000u) return 0;
    /* Switchroot's ptsa_rework scales the grant slope only above 2133 MHz.
     * Use the observed trained maximum, never the voltage-table ceiling. */
    if (policy==SW_EMC_PTSA_POLICY_REWORK && trained_max_khz>2133000u) {
        adjusted=(uint64_t)12500u*14998u*10u/(trained_max_khz/2u);
        lo=(uint32_t)(adjusted/10u+(adjusted%10u>=5u));
    }
    /* Preserve the upstream MHz truncation and intermediate rounding. */
    mhz=actual_khz/1000u;
    grant_fpa=lo*mhz*1000u/25000u;
    integer=grant_fpa>=10000u;
    if (integer) grant_fpa-=10000u;
    if (grant_fpa>=10000u) return 0;
    out->grant=(integer<<12)|sw_emc_ptsa_dda(grant_fpa);
    /* LPDDR4: 16 bytes/DRAM clock; CPU read reservation is 9% / 2.
     * The reference divides read_bw a second time in the write block after
     * using it; this has no effect on the 1% write reservation. */
    read_bw=(mhz*16u*9u/100u)/2u;
    write_bw=mhz*16u/100u;
    out->cpu_read=sw_emc_ptsa_dda(lo*read_bw/400u);
    out->cpu_write=sw_emc_ptsa_dda(lo*write_bw/400u);
    return out->grant!=0;
}

struct sw_emc_ptsa_write {
    uint32_t offset, mask, value;
};
#define SW_EMC_PTSA_PLAN_CAPACITY 60u

/* Build the B01 reset-state client configuration before touching hardware.
 * Caller owns the entire transaction: validate reset state and all assigned
 * resources, preserve reserved bits, latch/read back grant before enabling
 * client credits, and recover the preceding state on a partial failure.
 * This must not be applied to an arbitrary live/partially initialized MC.
 *
 * Client MIN/MAX values are from t21x_init_ptsa/program_ptsa. Unlike Linux's
 * initial zero display reservation, Windows supplies the active panel's
 * reservation and its parent first. FTOP's calculated write reservation is
 * also emitted explicitly; upstream calculates it but program_ptsa only
 * emits FTOP MIN/MAX, with later timing tables supplying its RATE.
 */
static inline int sw_emc_ptsa_bootstrap_plan(uint32_t actual_khz,
    uint32_t trained_max_khz, uint32_t display_rate, uint32_t ring_rate,
    struct sw_emc_ptsa_write *writes, uint32_t capacity, uint32_t *count)
{
    static const struct {
        uint16_t min_offset;
        int8_t minimum, maximum;
        int16_t rate; /* -1: preserve RATE, -2: CPU read, -3: CPU write */
    } clients[]={
        {0x42c,-5,31,0},  /* DISB */
        {0x438,-5,31,0},  /* VE */
        {0x498,-2, 0,0},  /* VE2: B01 */
        {0x444,-2, 0,12}, /* RING2 */
        {0x450,-4, 4,-2}, /* MLL_MPCORER */
        {0x45c, 1, 1,0},  /* SMMU PTSA scheduling, not translation registers */
        {0x4a4,-5,31,0},  /* ISP */
        {0x48c,-5,16,-1}, /* A9AVPPC */
        {0x4f8,-2, 0,-1}, /* AVP */
        {0x4c8,-2, 0,-1}, /* MSE */
        {0x540,-2, 0,-1}, /* GK */
        {0x558,-2, 0,-1}, /* VICPC */
        {0x4ec,-2, 0,-1}, /* APB */
        {0x4b0,-2, 0,-1}, /* PCX */
        {0x51c,-2, 0,-1}, /* HOST */
        {0x4e0,-2, 0,-1}, /* AHB */
        {0x4bc,-2, 0,-1}, /* SAX */
        {0x54c,-5,31,-1}, /* AUD */
        {0x4d4,-2, 0,-1}, /* SD */
        {0x528,-2, 0,-1}, /* USBX */
        {0x534,-2, 0,-1}, /* USBD */
        {0x510,-2, 0,-3}, /* FTOP */
    };
    struct sw_emc_ptsa_clock clock;
    uint32_t i,n=0;
    if (!count) return 0;
    *count=0;
    if (!writes || capacity<SW_EMC_PTSA_PLAN_CAPACITY ||
        !display_rate || display_rate>0xfffu || ring_rate>0xfffu ||
        /* Select STANDARD explicitly. Noble's opt-in does not establish the
         * Windows firmware contract; both policies agree at the current three
         * BPMP rates (<=1600 MHz). Higher-rate rework needs a matched pairing. */
        !sw_emc_ptsa_clock_values(actual_khz,trained_max_khz,SW_EMC_PTSA_POLICY_STANDARD,&clock) ||
        ring_rate<(display_rate+1u)/2u+clock.cpu_read+clock.cpu_write+12u)
        return 0;
#define SW_PTSA_PLAN(o,m,v) do { \
    if (n>=capacity) return 0; \
    writes[n++]=(struct sw_emc_ptsa_write){(o),(m),(v)}; \
} while (0)
    /* Match program_ptsa(): make decrement live before nonzero client rates.
     * With grant=0, enabling credits first could leave a client accumulating
     * priority across a preemption. The timing register is a command. */
    SW_PTSA_PLAN(0x960,0x1fff,clock.grant);
    SW_PTSA_PLAN(0x0fc,1u,1u);
    SW_PTSA_PLAN(0x480,0x3f,0x3b); /* RING1 parent before display child. */
    SW_PTSA_PLAN(0x484,0x3f,31);
    SW_PTSA_PLAN(0x47c,0xfff,ring_rate);
    for (i=0;i<sizeof(clients)/sizeof(clients[0]);++i) {
        SW_PTSA_PLAN(clients[i].min_offset,0x3f,(uint32_t)clients[i].minimum&0x3fu);
        SW_PTSA_PLAN(clients[i].min_offset+4u,0x3f,(uint32_t)clients[i].maximum);
        if (clients[i].rate!=-1) {
            uint32_t rate=clients[i].rate==-2 ? clock.cpu_read :
                clients[i].rate==-3 ? clock.cpu_write : (uint32_t)clients[i].rate;
            SW_PTSA_PLAN(clients[i].min_offset-4u,0xfff,rate);
        }
    }
    SW_PTSA_PLAN(0x420,0x3f,0x3b);
    SW_PTSA_PLAN(0x424,0x3f,31);
    SW_PTSA_PLAN(0x41c,0xfff,display_rate);
#undef SW_PTSA_PLAN
    *count=n;
    return 1;
}

struct sw_emc_ptsa_failure {
    uint32_t offset, expected, observed;
};
struct sw_emc_ptsa_io {
    void *context;
    int (*read)(void *, uint32_t offset, uint32_t *value);
    int (*write)(void *, uint32_t offset, uint32_t value);
    /* Optional first mismatch from a read already required by the transaction. */
    struct sw_emc_ptsa_failure *failure;
};
static inline void sw_emc_ptsa_record_mismatch(const struct sw_emc_ptsa_io *io,
    uint32_t offset, uint32_t expected, uint32_t observed)
{
    if (io->failure && !io->failure->offset)
        *io->failure=(struct sw_emc_ptsa_failure){offset,expected,observed};
}
enum sw_emc_ptsa_result {
    SwPtsaOk=0, SwPtsaInvalid=-1, SwPtsaState=-2,
    SwPtsaIo=-3, SwPtsaMismatch=-4, SwPtsaRollbackFailed=-5
};

/* The caller must serialize this with all BPMP clock work and validate that
 * every register has an assigned ACPI mapping before entering. This performs
 * no clock/voltage operation. It only accepts the reset state (grant/RATE=0,
 * MIN/MAX=-1). A pre-existing configuration belongs to the normal QoS checks,
 * not an excuse to overwrite another owner's settings.
 *
 * read/write perform ordered MMIO. MC_TIMING_CONTROL is a command: writing 1
 * follows program_ptsa(), not a fabricated persistent completion bit. All
 * persistent fields are read back before success. On any error, restore
 * clients before restoring zero grant, then request the timing latch again.
 */
static inline int sw_emc_ptsa_bootstrap(const struct sw_emc_ptsa_io *io,
    uint32_t actual_khz, uint32_t trained_max_khz,
    uint32_t display_rate, uint32_t ring_rate)
{
    struct sw_emc_ptsa_write plan[SW_EMC_PTSA_PLAN_CAPACITY];
    uint32_t before[SW_EMC_PTSA_PLAN_CAPACITY], count=0, i, value;
    uint32_t grant=0, cpu_read=0, cpu_write=0;
    int mtc_seed;
    int err=SwPtsaOk, restored=1;
    if (io && io->failure) *io->failure=(struct sw_emc_ptsa_failure){0};
    if (!io || !io->read || !io->write ||
        !sw_emc_ptsa_bootstrap_plan(actual_khz,trained_max_khz,
            display_rate,ring_rate,plan,SW_EMC_PTSA_PLAN_CAPACITY,&count))
        return SwPtsaInvalid;
    for (i=0;i<count;++i) {
        if (plan[i].offset==0xfc) { before[i]=0; continue; }
        if (!io->read(io->context,plan[i].offset,&before[i]) || before[i]==UINT32_MAX)
            return SwPtsaIo;
        if (plan[i].offset==0x960) grant=before[i]&plan[i].mask;
        if (plan[i].offset==0x44c) cpu_read=before[i]&plan[i].mask;
        if (plan[i].offset==0x50c) cpu_write=before[i]&plan[i].mask;
    }
    mtc_seed=sw_emc_ptsa_mtc_seed(actual_khz,trained_max_khz,grant,cpu_read,cpu_write);
    for (i=0;i<count;++i) {
        if (plan[i].offset==0xfc) continue;
        value=plan[i].mask==0x3fu ? 0x3fu : 0u;
        if (mtc_seed) {
            if (plan[i].offset==0x960) value=6143u;
            if (plan[i].offset==0x44c) value=277u;
            if (plan[i].offset==0x50c) value=31u;
        }
        if ((before[i]&plan[i].mask)!=value) return SwPtsaState;
    }
    /* MIN/MAX changes would also enable a pre-existing RATE on clients whose
     * RATE Linux preserves. Prove those credits are zero before accepting a
     * reset-state bootstrap; do not accidentally promote an unknown client. */
    for (i=0;i+1u<count;++i) {
        uint32_t j, offset;
        if (plan[i].mask!=0x3fu || plan[i+1u].mask!=0x3fu ||
            plan[i+1u].offset!=plan[i].offset+4u) continue;
        offset=plan[i].offset-4u;
        for (j=0;j<count && plan[j].offset!=offset;++j) {}
        if (j<count) continue;
        if (!io->read(io->context,offset,&value) || value==UINT32_MAX) return SwPtsaIo;
        if (value&0xfffu) return SwPtsaState;
    }
    if (!io->read(io->context,0x2acu,&value) || value==UINT32_MAX) return SwPtsaIo;
    if (value) return SwPtsaState; /* DIS_EXTRA_SNAP_LEVELS: retain reset value. */
    for (uint32_t n=0;n<count;++n) {
        /* Seeded CPU credits already exist. Supply the full parent first,
         * while the old nonzero grant is live, then establish the new grant.
         * Zero reset keeps the original grant-first ordering. */
        static const uint8_t seeded_prefix[5]={4,2,3,0,1};
        i=mtc_seed && n<5u ? seeded_prefix[n] : n;
        uint32_t check=i, expected;
        value=(before[i]&~plan[i].mask)|plan[i].value;
        if (!io->write(io->context,plan[i].offset,value)) { err=SwPtsaIo; break; }
        /* program_ptsa writes grant followed by MC_TIMING_CONTROL. A read of
         * the active grant before that latch may still return the old value.
         * Verify it after the pair, before any client gains nonzero credits. */
        if (plan[i].offset==0x960 || (mtc_seed &&
            (plan[i].offset==0x44c || plan[i].offset==0x50c))) continue;
        if (plan[i].offset==0xfc) check=0;
        expected=(before[check]&~plan[check].mask)|plan[check].value;
        if (!io->read(io->context,plan[check].offset,&value)) { err=SwPtsaIo; break; }
        if (value!=expected) {
            sw_emc_ptsa_record_mismatch(io,plan[check].offset,expected,value);
            err=SwPtsaMismatch; break;
        }
    }
    /* Complete any clock-term shadows before verifying the seeded takeover. */
    if (!err && mtc_seed && !io->write(io->context,0xfcu,1u)) err=SwPtsaIo;
    if (!err) {
        for (i=0;i<count;++i) {
            if (plan[i].offset==0xfc) continue;
            if (!io->read(io->context,plan[i].offset,&value)) { err=SwPtsaIo; break; }
            if (value!=((before[i]&~plan[i].mask)|plan[i].value)) {
                sw_emc_ptsa_record_mismatch(io,plan[i].offset,
                    (before[i]&~plan[i].mask)|plan[i].value,value);
                err=SwPtsaMismatch; break;
            }
        }
    }
    if (!err) return SwPtsaOk;
    /* A failed write may still have reached the device. Restore the complete
     * saved state, including the attempted field, rather than trusting how
     * many writes the transport reported as successful. */
    for (i=count;i-- > 0;) {
        if (plan[i].offset!=0xfc && !io->write(io->context,plan[i].offset,before[i]))
            restored=0;
    }
    if (!io->write(io->context,0xfc,1u)) restored=0;
    for (i=0;i<count;++i) {
        if (plan[i].offset==0xfc) continue;
        if (!io->read(io->context,plan[i].offset,&value) || value!=before[i]) restored=0;
    }
    return restored ? err : SwPtsaRollbackFailed;
}
#endif
