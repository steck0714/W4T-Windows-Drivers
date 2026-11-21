# Linux v6.12 の nouveau(GM20B まわり) — 上流のままの参照コピー

上流: https://github.com/torvalds/linux  タグ `v6.12`  コミット `adc218676eef25575469234709c2d87185ca223a`(取得 2026-11-20、sparse checkout)。ライセンス: **GPL-2.0**(`COPYING`)。nouveau の各ファイルは先頭の SPDX / 許諾文に従う(多くは MIT、Red Hat ほか)。**無改変**。

- 中身: `drivers/gpu/drm/nouveau/nvkm/subdev/{fb,mmu,acr,pmu,ltc,bus,privring}`、`nvkm/falcon`、`include/nvkm/{subdev/{fb,mmu,acr,pmu}.h,falcon.h}`、`COPYING`、`LICENSES/`。
- 使いどころ: `mmu/vmmgf100.c` の `gf100_vmm_pgd_pde()`(PDE の書き方。低位 32bit が大ページ用、高位 32bit が 4KiB 用のページテーブル)、`vmmgm200.c` / `vmmgm20b.c`(64KiB 構成と PDB bit11)、`vmmgk20a.c`(`gk20a_vmm_aper`)、`fb/gm200.c`(`init_page`)、`falcon/gm200.c`(`fw_load`)。
- 既存の `reference/l4t/linux-mainline/nouveau-volt/`(電圧まわり)とは別。
- 全ファイルの SHA-256: `MANIFEST.sha256`(この `README.md` と自身を除く)。
