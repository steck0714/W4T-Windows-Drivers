# CTCaer/switch-l4t-kernel-nvgpu の追加分 — 上流のままの参照コピー

上流: https://github.com/CTCaer/switch-l4t-kernel-nvgpu  固定コミット `1ae0167d360287ca78f5a2572f0de42594140312`(取得 2026-11-20)。ライセンス: **MIT**(NVIDIA。各ファイル先頭の許諾文を消さない。全文は `LICENSES/MIT.txt`)。
既存の `reference/l4t/nvgpu/`(`gm20b/`、`hw-gm20b/`、`os-linux/`)に無かった分を、上流のパスのまま無改変で置いた。`reference/l4t/MANIFEST.sha256` には触っていない。

- 中身: `common/{fb,falcon,mm,pmu,priv_ring,ltc,mc,bus,fuse,clock_gating,init}`、`gk20a/{flcn,mm,pmu,fb,priv_ring,ltc,mc,bus,fifo}_gk20a.[ch]` と `gk20a/gk20a.[ch]`(あるものだけ)。
- 使いどころ: `common/fb/fb_gm20b.c`(MMU_CTRL bit11、`gm20b_fb_set_mmu_page_size`)、`gk20a/flcn_gk20a.c`(ACR ブートローダーの bootstrap)、`gk20a/mm_gk20a.c`(インスタンスブロックの PDB / VA limit)。hayato41200 の `COMPARE-B3c-ja.md` が比べている箇所。
- 全ファイルの SHA-256: `MANIFEST.sha256`(この `README.md` と自身を除く)。
