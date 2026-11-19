/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2014-2019 NVIDIA CORPORATION.
 * Copyright (c) 2021-2023 CTCaer.
 * PTSA terms: tegra21x_la.c at 76e6d48970b451c242c20f298b8d63027836bb0b.
 * Clock boundary ordering: Hekate sys_sdrammtc.c at
 * 8e1392df4569ac501a115867056b13c361429e9e, Steps 8 and 25.
 * Caller owns these PTSA fields and serializes the whole BPMP transaction.
 * Lower terms before a downshift; raise them after a confirmed upshift.
 * Ordinary scaling rejects zero grant. A separate post-clock operation below
 * handles only the complete zero clock tuple observed on B01 firmware.
 */
#ifndef SW_T214_EMC_PTSA_SCALE_H
#define SW_T214_EMC_PTSA_SCALE_H
#include "emc_ptsa.h"
static const uint32_t sw_ptsa_scale_offsets[4]={0x44c,0x50c,0x47c,0x960};

static inline int sw_ptsa_scale_verify(const struct sw_emc_ptsa_io *io,
                                      const uint32_t expected[4])
{
    for (uint32_t i=0;i<4u;++i) {
        uint32_t value;
        if (!io->read(io->context,sw_ptsa_scale_offsets[i],&value)) return SwPtsaIo;
        if (value!=expected[i]) {
            sw_emc_ptsa_record_mismatch(io,sw_ptsa_scale_offsets[i],expected[i],value);
            return SwPtsaMismatch;
        }
    }
    return SwPtsaOk;
}
static inline int sw_ptsa_scale_budget_valid(const uint32_t values[4],
    uint32_t display, uint32_t iso, uint32_t ring2)
{
    uint32_t rd=values[0]&0xfffu, wr=values[1]&0xfffu;
    uint32_t ring=values[2]&0xfffu, grant=values[3]&0x1fffu;
    return grant && display*4u<grant && ring*2u<grant &&
        ring >= (display+iso+1u)/2u+rd+wr+ring2;
}
static inline int sw_emc_ptsa_scale(const struct sw_emc_ptsa_io *io,
    const struct sw_emc_ptsa_clock *clock, uint32_t ring)
{
    /* 4 means MC_TIMING_CONTROL. Latch children before lowering their parent,
     * and latch grant before increasing parent/child credits. CPU terms may
     * have shadow registers too, so do not verify them before their latch. */
    static const uint8_t up[6]={3,4,2,0,1,4};
    static const uint8_t down[6]={0,1,4,2,3,4};
    static const uint32_t extra_offsets[6]={0x41c,0x428,0x434,0x494,0x4a0,0x440};
    uint32_t before[4], after[4], expected[4], extra[6], iso=0;
    const uint8_t *order, *restore_order;
    int err=SwPtsaOk, restored=1, identical=1;
    if (io && io->failure) *io->failure=(struct sw_emc_ptsa_failure){0};
    if (!io || !io->read || !io->write || !clock ||
        !clock->grant || clock->grant>0x1fffu || clock->cpu_read>0xfffu ||
        clock->cpu_write>0xfffu || ring>0xfffu) return SwPtsaInvalid;
    for (uint32_t i=0;i<4u;++i) {
        if (!io->read(io->context,sw_ptsa_scale_offsets[i],&before[i]) ||
            before[i]==UINT32_MAX) return SwPtsaIo;
        expected[i]=before[i];
    }
    for (uint32_t i=0;i<6u;++i) {
        if (!io->read(io->context,extra_offsets[i],&extra[i]) ||
            extra[i]>0xfffu) return SwPtsaIo;
        if (i>0 && i<5u) iso+=extra[i];
    }
    after[0]=(before[0]&~0xfffu)|clock->cpu_read;
    after[1]=(before[1]&~0xfffu)|clock->cpu_write;
    after[2]=(before[2]&~0xfffu)|ring;
    after[3]=(before[3]&~0x1fffu)|clock->grant;
    if (!sw_ptsa_scale_budget_valid(before,extra[0],iso,extra[5])) return SwPtsaState;
    if (!sw_ptsa_scale_budget_valid(after,extra[0],iso,extra[5])) return SwPtsaInvalid;
    for (uint32_t i=0;i<4u;++i) identical=identical && before[i]==after[i];
    if (identical) return SwPtsaOk;
    order=clock->grant>=(before[3]&0x1fffu) ? up : down;
    restore_order=order==up ? down : up;
    /* Validate every visible intermediate state before the first write.
     * This also rejects a caller mixing unrelated reservation changes into
     * a nominal clock-only update. */
    for (uint32_t n=0;n<6u;++n) {
        uint32_t i=order[n];
        if (i<4u) expected[i]=after[i];
        if (!sw_ptsa_scale_budget_valid(expected,extra[0],iso,extra[5])) return SwPtsaInvalid;
    }
    for (uint32_t i=0;i<4u;++i) expected[i]=before[i];
    for (uint32_t n=0;n<6u;++n) {
        uint32_t i=order[n];
        if (i==4u) {
            if (!io->write(io->context,0xfcu,1u)) { err=SwPtsaIo; break; }
            err=sw_ptsa_scale_verify(io,expected);
            if (err) break;
        } else {
            if (!io->write(io->context,sw_ptsa_scale_offsets[i],after[i])) { err=SwPtsaIo; break; }
            expected[i]=after[i];
            if (i==2u) {
                uint32_t actual;
                if (!io->read(io->context,0x47cu,&actual)) { err=SwPtsaIo; break; }
                if (actual!=after[2]) {
                    sw_emc_ptsa_record_mismatch(io,0x47cu,after[2],actual);
                    err=SwPtsaMismatch; break;
                }
            }
        }
    }
    if (!err) return SwPtsaOk;
    /* A failed write may already have arrived. Restore the complete tuple;
     * use the opposite direction so the old parent/children remain covered. */
    for (uint32_t n=0;n<6u;++n) {
        uint32_t i=restore_order[n];
        if (!io->write(io->context,i==4u ? 0xfcu : sw_ptsa_scale_offsets[i],
                       i==4u ? 1u : before[i])) restored=0;
    }
    if (sw_ptsa_scale_verify(io,before)) restored=0;
    return restored ? err : SwPtsaRollbackFailed;
}
/* Fan27 native readback after SET_RATE(204000), 2026-11-15: grant/CPU read/
 * CPU write became exactly zero while DIS/RING1/other clients/LA stayed intact.
 * The caller must first confirm the requested physical clock and unchanged
 * non-clock fields for its pending transition. This is not reset bootstrap:
 * only the three clock-owned words are written, never the parent or clients. */
static inline int sw_emc_ptsa_finish_zero_clock(const struct sw_emc_ptsa_io *io,
    const struct sw_emc_ptsa_clock *clock, uint32_t ring)
{
    static const uint8_t forward[5]={3,4,0,1,4};
    static const uint8_t rollback[5]={0,1,4,3,4};
    static const uint32_t extra_offsets[6]={0x41c,0x428,0x434,0x494,0x4a0,0x440};
    uint32_t before[4], after[4], expected[4], extra[6], iso=0;
    int err=SwPtsaOk, restored=1;
    if (io && io->failure) *io->failure=(struct sw_emc_ptsa_failure){0};
    if (!io || !io->read || !io->write || !clock || !clock->grant ||
        clock->grant>0x1fffu || clock->cpu_read>0xfffu || clock->cpu_write>0xfffu ||
        ring>0xfffu) return SwPtsaInvalid;
    for (uint32_t i=0;i<4u;++i) {
        if (!io->read(io->context,sw_ptsa_scale_offsets[i],&before[i]) ||
            before[i]==UINT32_MAX) return SwPtsaIo;
        expected[i]=before[i];
    }
    if (before[0] || before[1] || before[3] || (before[2]&0xfffu)!=ring)
        return SwPtsaState;
    for (uint32_t i=0;i<6u;++i) {
        if (!io->read(io->context,extra_offsets[i],&extra[i]) || extra[i]>0xfffu)
            return SwPtsaIo;
        if (i>0 && i<5u) iso+=extra[i];
    }
    after[0]=clock->cpu_read; after[1]=clock->cpu_write;
    after[2]=before[2]; after[3]=clock->grant;
    if (!sw_ptsa_scale_budget_valid(after,extra[0],iso,extra[5])) return SwPtsaInvalid;
    for (uint32_t n=0;n<5u;++n) {
        const uint32_t i=forward[n];
        if (i==4u) {
            if (!io->write(io->context,0xfcu,1u)) { err=SwPtsaIo; break; }
            err=sw_ptsa_scale_verify(io,expected);
            if (err) break;
        } else {
            if (!io->write(io->context,sw_ptsa_scale_offsets[i],after[i])) { err=SwPtsaIo; break; }
            expected[i]=after[i];
        }
    }
    if (!err) return SwPtsaOk;
    /* Remove child credits before restoring zero grant. Return failure even
     * after restoring the observed tuple; it is not a usable completed point. */
    for (uint32_t n=0;n<5u;++n) {
        const uint32_t i=rollback[n];
        if (!io->write(io->context,i==4u ? 0xfcu : sw_ptsa_scale_offsets[i],
                       i==4u ? 1u : before[i])) restored=0;
    }
    if (sw_ptsa_scale_verify(io,before)) restored=0;
    return restored ? err : SwPtsaRollbackFailed;
}
#endif
