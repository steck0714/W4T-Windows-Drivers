# L4T / Switchroot / hekate ソースの同梱

調査に使った上流ソースを、**各ファイルのライセンス表記を変更せず**、そのまま同梱する。
実装の根拠を後から検証できるようにするための参照用コピーで、ビルド対象ではない。

| ディレクトリ | 上流 | 固定コミット | ライセンス |
|---|---|---|---|
| `hekate/` (`bdk/power`, `bdk/input`, `bdk/soc/{i2c,pmc}`) | https://github.com/CTCaer/hekate | `8e1392df4569ac501a115867056b13c361429e9e` | GPL-2.0（`hekate/LICENSE`。各ファイルの著作権表示は CTCaer / naehrwert / shchmue / Billy Laws ほか） |
| `nvgpu/gm20b/` | https://github.com/CTCaer/switch-l4t-kernel-nvgpu (`drivers/gpu/nvgpu/gm20b`) | `1ae0167d360287ca78f5a2572f0de42594140312` | MIT（NVIDIA CORPORATION、各ファイル冒頭の許諾文） |
| `nvgpu/hw-gm20b/` | 同上 (`include/nvgpu/hw/gm20b`) | 同上 | MIT（NVIDIA CORPORATION、各ファイル冒頭の許諾文） |
| `nvgpu/os-linux/` | 同上 (`drivers/gpu/nvgpu/os/linux/platform_gk20a_tegra.[ch]`) | 同上 | GPL-2.0（NVIDIA CORPORATION、各ファイル冒頭） |
| `linux-mainline/nouveau-volt/` | `drivers/gpu/drm/nouveau/nvkm/subdev/volt/{gm20b.c,gk20a.c,gk20a.h}`（Erista GM20B の CVB 係数表と電圧式） | torvalds/linux `666539eecdf4db623f5e3c3e55cf9f2dfad24aa4` | NVIDIA CORPORATION（MIT、各ファイル冒頭） |
| `linux-mainline/tegra-fuse/` | `drivers/soc/tegra/fuse/speedo-tegra210.c`（GPU speedo の fuse 位置と換算式） | 同上 | NVIDIA CORPORATION（GPL-2.0-only、SPDX 表記） |
| `platform-t210-nx/` | https://github.com/CTCaer/switch-l4t-platform-t210-nx (`kernel-dts`: PMIC・電源ツリー・common dtsi、vali.dts) | `cf785c4c176499b301170d79fe57b77f365b73cd` | GPL-2.0（Billy Laws / CTCaer、各ファイル冒頭） |

## 再配布の手順
条件の詳細と守っていること: `NOTICE.md`。ライセンス全文: `LICENSES/`。改ざん・取り違えの検査: `python3 tools/check_l4t_reference.py`（`MANIFEST.sha256` と各ファイルの許諾ヘッダを確認）。
nvgpu と platform-t210-nx には上流に LICENSE ファイルが無く、ライセンスは各ファイル冒頭で示される。

## 使い方・注意
- 本リポジトリは GPL-2.0（ルートの `LICENSE`）。MIT 部分（NVIDIA）は GPL-2.0 と両立するが、**ファイルごとの許諾文と著作権表示は消さない**こと。
- NVIDIA の GPU ファームウェア（`NETB_img.bin` など 9 ファイル）は含めない。
- どのドライバのどの値が、どのファイルのどの行に対応するかは `CREDITS.md` と各ドライバの `NOTICE` / README を参照。
- 同梱したのは調査で参照した範囲のみで、各上流リポジトリ全体ではない。全体は上記 URL を参照。
