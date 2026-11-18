/* SPDX-License-Identifier: GPL-2.0-only
 * DisplayPort stream/link math for Tegra210 SOR (Switch dock output).
 * Port of tegra_dc_dp_calc_config() from NVIDIA L4T (reference/nvidia-dc/dp.c, switch-l4t-kernel-nvidia
 * 76e6d489; Copyright NVIDIA CORPORATION, CTCaer; GPL-2.0). Pure integer math, no WDK/Linux deps.
 * Verified against the original by tests/test_dp_link_diff.py. Register writes are NOT here. */
#ifndef DP_LINK_H
#define DP_LINK_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t h_active, h_front_porch, h_back_porch, h_sync_width; } dp_mode;
typedef struct {
    uint32_t link_bw;          /* DPCD LINK_BW_SET units of 0.27 Gbps/lane: 6=RBR 10=HBR 20=HBR2 */
    uint32_t lane_count, bits_per_pixel, enhanced_framing;
    bool     is_valid;
    uint32_t activepolarity, active_count, tu_size, active_frac, watermark;
    int32_t  hblank_sym, vblank_sym;
} dp_link_cfg;

static inline bool dp_calc_config(uint32_t pclk_hz, const dp_mode *mode, dp_link_cfg *cfg)
{
    const uint32_t link_rate = 27u * cfg->link_bw * 1000u * 1000u;
    const uint64_t f = 100000;
    uint32_t num_linkclk_line, activecount, activepolarity;
    uint64_t ratio_f, frac_f, activesym_f, activecount_f, approx_value_f;
    uint32_t activefrac = 0;
    uint64_t accumulated_error_f = 0;
    uint32_t lowest_neg_activecount = 0, lowest_neg_activepolarity = 0, lowest_neg_tusize = 64;
    uint32_t num_symbols_per_line;
    uint64_t lowest_neg_activefrac = 0, lowest_neg_error_f = 64 * f, watermark_f;
    int i; bool neg;
    const uint32_t rate = pclk_hz;

    cfg->is_valid = false;
    if (!link_rate || !cfg->lane_count || !rate || !cfg->bits_per_pixel) return false;
    if ((uint64_t)rate * cfg->bits_per_pixel >= (uint64_t)link_rate * 8 * cfg->lane_count) return false;

    num_linkclk_line = (uint32_t)(((uint64_t)link_rate * mode->h_active) / rate);
    ratio_f = (uint64_t)rate * cfg->bits_per_pixel * f;
    ratio_f /= 8;
    ratio_f = ratio_f / ((uint64_t)(uint32_t)(link_rate * cfg->lane_count));

    for (i = 64; i >= 32; --i) {
        activesym_f = ratio_f * (uint64_t)i;
        activecount_f = (activesym_f / (uint32_t)f) * f;
        frac_f = activesym_f - activecount_f;
        activecount = (uint32_t)(activecount_f / (uint32_t)f);
        if (frac_f < (f / 2)) activepolarity = 0;
        else { activepolarity = 1; frac_f = f - frac_f; }
        if (frac_f != 0) {
            frac_f = (f * f) / frac_f;
            if (frac_f > (15 * f)) activefrac = activepolarity ? 1 : 15;
            else activefrac = activepolarity ? (uint32_t)(frac_f / (uint32_t)f) + 1
                                             : (uint32_t)(frac_f / (uint32_t)f);
        }
        if (activefrac == 1) activepolarity = 0;
        if (activepolarity == 1)
            approx_value_f = activefrac ? (activecount_f + (activefrac * f - f) * f) / (activefrac * f)
                                        : activecount_f + f;
        else
            approx_value_f = activefrac ? activecount_f + f / activefrac : activecount_f;
        if (activesym_f < approx_value_f) {
            accumulated_error_f = (uint64_t)num_linkclk_line * ((approx_value_f - activesym_f) / (uint32_t)i);
            neg = true;
        } else {
            accumulated_error_f = (uint64_t)num_linkclk_line * ((activesym_f - approx_value_f) / (uint32_t)i);
            neg = false;
        }
        if ((neg && (lowest_neg_error_f > accumulated_error_f)) || (accumulated_error_f == 0)) {
            lowest_neg_error_f = accumulated_error_f;
            lowest_neg_tusize = (uint32_t)i;
            lowest_neg_activecount = activecount;
            lowest_neg_activepolarity = activepolarity;
            lowest_neg_activefrac = activefrac;
            if (accumulated_error_f == 0) break;
        }
    }
    if (lowest_neg_activefrac == 0) {
        cfg->activepolarity = 0;
        cfg->active_count = lowest_neg_activepolarity ? lowest_neg_activecount : lowest_neg_activecount - 1;
        cfg->tu_size = lowest_neg_tusize;
        cfg->active_frac = 1;
    } else {
        cfg->activepolarity = lowest_neg_activepolarity;
        cfg->active_count = lowest_neg_activecount;
        cfg->tu_size = lowest_neg_tusize;
        cfg->active_frac = (uint32_t)lowest_neg_activefrac;
    }
    watermark_f = (ratio_f * cfg->tu_size * (f - ratio_f)) / f;
    cfg->watermark = (uint32_t)((watermark_f + lowest_neg_error_f) / f) + cfg->bits_per_pixel / 4 - 1;
    num_symbols_per_line = (mode->h_active * cfg->bits_per_pixel) / (8 * cfg->lane_count);
    if (cfg->watermark > 30) { cfg->watermark = 30; return false; }
    else if (cfg->watermark > num_symbols_per_line) { cfg->watermark = num_symbols_per_line; return false; }

    cfg->hblank_sym = (int32_t)((((uint64_t)(mode->h_back_porch + mode->h_front_porch + mode->h_sync_width - 7)
                        * link_rate) / rate)) - 3 * (int32_t)cfg->enhanced_framing - (int32_t)(12 / cfg->lane_count);
    if (cfg->hblank_sym < 0) cfg->hblank_sym = 0;
    cfg->vblank_sym = (int32_t)(((uint64_t)(mode->h_active - 25) * link_rate) / rate) - (int32_t)(36 / cfg->lane_count) - 4;
    if (cfg->vblank_sym < 0) cfg->vblank_sym = 0;
    cfg->is_valid = true;
    return true;
}

/* Lowest-bandwidth valid (link_bw, lanes) for a mode, like the explore loop in dp.c: try RBR/HBR/HBR2,
 * then 1/2/4 lanes (never above max_lanes / max_bw). Returns false if none fits. */
static inline bool dp_pick_link(uint32_t pclk_hz, const dp_mode *m, uint32_t bpp,
                                uint32_t max_lanes, uint32_t max_bw, dp_link_cfg *out)
{
    static const uint32_t bws[3] = {6, 10, 20};
    uint32_t lanes, b;
    for (b = 0; b < 3; b++) {
        if (bws[b] > max_bw) break;
        for (lanes = 1; lanes <= 4 && lanes <= max_lanes; lanes <<= 1) {
            dp_link_cfg c = {0}; c.link_bw = bws[b]; c.lane_count = lanes; c.bits_per_pixel = bpp;
            if (dp_calc_config(pclk_hz, m, &c)) { *out = c; return true; }
        }
    }
    return false;
}
#endif
