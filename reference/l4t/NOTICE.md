# reference/l4t の再配布について（NOTICE）

このディレクトリの中身は、上流プロジェクトのファイルを**一切変更せず（バイト単位で同一）**にコピーしたものです。
ビルド対象ではなく、実装の根拠を後から検証するための参照用です。固定コミットは `SOURCES.md`、
各ファイルの SHA-256 は `MANIFEST.sha256` にあり、`tools/check_l4t_reference.py` で検査できます。

## ライセンスごとの扱い
| 対象 | ライセンス | 守っていること |
|---|---|---|
| `hekate/`、`platform-t210-nx/`、`nvgpu/os-linux/`、`linux-mainline/tegra-fuse/` | GPL-2.0 | GPL-2.0 第1条: 各コピーに著作権表示・無保証表示を残し（ファイル冒頭をそのまま維持）、ライセンス全文を添付（`LICENSES/GPL-2.0.txt`）。第2条: 変更していない（変更した場合は変更ファイルに明示が必要）。第3条: このリポジトリはソースそのものを配布しており、ここから作ったバイナリ（`.sys` 等）を配る場合は、対応するソース（このリポジトリの該当コミット）を同じ場所で提供するか、書面での提供申出を付ける。 |
| `nvgpu/gm20b/`、`nvgpu/hw-gm20b/`、`linux-mainline/nouveau-volt/` | MIT（NVIDIA CORPORATION） | 著作権表示と許諾文を全コピーに残す（ファイル冒頭をそのまま維持。全文は `LICENSES/MIT-NVIDIA.txt`）。 |

## 上流にライセンスファイルが無いもの
- `CTCaer/switch-l4t-kernel-nvgpu` と `CTCaer/switch-l4t-platform-t210-nx` のリポジトリ直下には LICENSE/COPYING が無い（2026-11-17 に確認）。
  ライセンスは**各ファイル冒頭の許諾文**で示されているため、そのヘッダを消さないことが条件になる。
- `hekate` には直下に `LICENSE`（GPL-2.0）がある。
- `linux-mainline/tegra-fuse/speedo-tegra210.c` は SPDX 表記（`GPL-2.0-only`）で示される。Linux の `COPYING` / `LICENSES/` は同梱せず、GPL-2.0 全文は `LICENSES/GPL-2.0.txt` を参照。

## 含めないもの
- NVIDIA の GPU ファームウェア（`NETB_img.bin` ほか）と L4T の配布バイナリ。これらは上記のソースライセンスとは別の条件で、再配布可否を確認していないため。
- 上流リポジトリ全体。参照した範囲のみ同梱（全体は `SOURCES.md` の URL）。

## このリポジトリ側のコードとの関係
`drivers/t210-icosa/drivers/TegraGpu/platform/t210_power.c` などは、上記の値・手順を**参照して書き直した**もの（コピーではない）。
GPL-2.0 部分を含むため、配布時は `drivers/*/TegraGpu/NOTICE` とルートの `LICENSE`・`CREDITS.md` に従う。
