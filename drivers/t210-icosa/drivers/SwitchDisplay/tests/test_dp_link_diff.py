#!/usr/bin/env python3
"""Differential test: NVIDIA's tegra_dc_dp_calc_config() (extracted verbatim from
reference/nvidia-dc/dp.c) vs the port in src/dp_link.h, over a grid of modes/links."""
import pathlib, re, subprocess, sys, tempfile
here = pathlib.Path(__file__).resolve().parent
root = here.parents[4]
src = (root / "reference/nvidia-dc/dp.c").read_text()
m = re.search(r"^bool tegra_dc_dp_calc_config\(.*?^\treturn true;\n\}", src, re.S | re.M)
if not m: print("could not extract original function"); sys.exit(1)
orig = m.group(0).replace("tegra_dc_dp_calc_config", "orig_calc_config", 1)
harness = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "dp_link.h"
typedef uint32_t u32; typedef uint64_t u64; typedef uint8_t u8;
#define dev_dbg(...) ((void)0)
struct tegra_dc; struct tegra_dc_dp_data { struct tegra_dc *dc; };
struct tegra_dc_mode { u32 h_active, h_front_porch, h_back_porch, h_sync_width; };
struct tegra_dc_dp_link_config { bool is_valid; u32 link_bw, lane_count, bits_per_pixel, enhanced_framing,
  activepolarity, active_count, tu_size, active_frac, watermark; int hblank_sym, vblank_sym; };
static unsigned long g_rate;
static unsigned long tegra_dc_clk_get_rate(struct tegra_dc *dc) { (void)dc; return g_rate; }
static int tegra_dc_is_t19x(void) { return 0; }
static int tegra_platform_is_fpga(void) { return 0; }
static u64 tegra_div64(u64 a, u32 b) { return a / b; }
''' + orig + r'''
int main(void) {
  static const u32 pclk[] = {25175000, 74250000, 148500000, 241500000, 297000000, 533250000};
  static const dp_mode modes[] = {{640,16,48,96},{1280,110,220,40},{1920,88,148,44},{2560,48,80,32},{3840,176,296,88}};
  static const u32 bws[] = {6,10,20}, lanes[] = {1,2,4}, bpps[] = {18,24,30,36};
  long n = 0, valid = 0, bad = 0;
  for (unsigned a=0;a<6;a++) for (unsigned b=0;b<5;b++) for (unsigned c=0;c<3;c++) for (unsigned d=0;d<3;d++)
  for (unsigned e=0;e<4;e++) for (u32 ef=0; ef<2; ef++) {
    struct tegra_dc_dp_data dp = {0}; struct tegra_dc_mode om = {modes[b].h_active, modes[b].h_front_porch, modes[b].h_back_porch, modes[b].h_sync_width};
    struct tegra_dc_dp_link_config o = {0}; dp_link_cfg p = {0};
    o.link_bw = p.link_bw = bws[c]; o.lane_count = p.lane_count = lanes[d]; o.bits_per_pixel = p.bits_per_pixel = bpps[e];
    o.enhanced_framing = p.enhanced_framing = ef; g_rate = pclk[a];
    bool ro = orig_calc_config(&dp, &om, &o); bool rp = dp_calc_config(pclk[a], &modes[b], &p);
    n++; valid += ro;
    if (ro != rp || o.is_valid != p.is_valid || o.watermark != p.watermark || (ro && (o.tu_size != p.tu_size ||
        o.active_count != p.active_count || o.active_frac != p.active_frac || o.activepolarity != p.activepolarity ||
        o.hblank_sym != p.hblank_sym || o.vblank_sym != p.vblank_sym))) {
      if (bad++ < 5) printf("DIFF pclk=%u h=%u bw=%u lanes=%u bpp=%u ef=%u\n", pclk[a], modes[b].h_active, bws[c], lanes[d], bpps[e], ef);
    }
  }
  /* sanity: 1080p60 24bpp must be valid on 4 lanes RBR and be picked by dp_pick_link */
  dp_link_cfg pick; dp_mode m1080 = {1920,88,148,44};
  int ok = dp_pick_link(148500000, &m1080, 24, 4, 20, &pick);
  printf("cases=%ld valid=%ld mismatches=%ld pick1080p=%d(bw=%u lanes=%u tu=%u wm=%u)\n", n, valid, bad, ok, pick.link_bw, pick.lane_count, pick.tu_size, pick.watermark);
  return (bad == 0 && valid > 100 && ok) ? 0 : 1;
}
'''
with tempfile.TemporaryDirectory() as td:
    td = pathlib.Path(td); (td / "h.c").write_text(harness)
    cc = subprocess.run(["cc", "-std=gnu99", "-O1", "-Wno-unused-function", "-I", str(here.parent / "src"), str(td / "h.c"), "-o", str(td / "h")],
                        capture_output=True, text=True)
    if cc.returncode: print(cc.stderr[-1500:]); sys.exit(1)
    r = subprocess.run([str(td / "h")], capture_output=True, text=True); print(r.stdout.strip()); sys.exit(r.returncode)
