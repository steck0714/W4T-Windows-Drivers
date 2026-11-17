# Erista (T210) GPU ドライバ統合メモ

取り込み元: `gpu_erista.zip`（ユーザー提供）。配置:
- `drivers/t210-icosa/drivers/TegraGpu/` ドライバ本体（core / platform / windows / tests / tools）
- `acpi/t210/` `gpu0.asl.inc`（ACPI\SWOA0F02）と `apply_gpu_patch.py`。適用済みで、`-D SWITCH_ERISTA_GPU=1` のときだけ有効
- `tools/check_acpi_resources.py`、`.github/workflows/build-erista-gpu.yml`
- 元の README: `docs/ERISTA-GPU-upstream-README.md`、ライセンス注記: `TegraGpu/NOTICE`（GPL-2.0 系の電源/クロック部と MIT 系の nouveau/nvgpu 由来部が混在）

## 今回の照合結果
| 対象 | 照合先 | 結果 |
|---|---|---|
| MAX77621(0x1C, I2C5)、MAX77620 GPIO6、電圧式 606.25mV+6.25mV×code、CONTROL1=0xB0/CONTROL2=0xC1、HIDREV 判定 | hekate | 一致 |
| GPCPLL のレジスタ・順序・SEL_VCO・bypass | Switchroot nvgpu `clk_gm20b.c`（1ae0167） | 順序は一致。**2点を修正**（下記） |
| メモリ窓・IRQ・I2C 接続順 | `check_acpi_resources.py` | 一致 |
| I2C 接続の共有指定（[0]=0x1C 排他、[1]=0x3C 共有） | ACPI と `platform_io.c` の open 指定 | 整合 |
| mesa-switch エンコーダ | 0ff52079 に固定、ホストテスト全通過 | 一致 |

### 修正した不具合
1. `t210_clock.c`: PLL 有効化後、ロック待ちの前にロック検出（CFG bit4 `enb_lckdet_power_off`）を入れる処理が無かった。nvgpu と同じく読み出して bit4 が立っていれば落とす。
2. ロック待ちが 300µs で、nvgpu の 501 回（500+1）より短かった。501µs に変更。
3. `tests/test_clock.c` にロック検出 OFF の初期状態のケースを追加。

## 追加照合（2026-11-17）
### Switchroot の DTS / nvgpu（Erista = odin）
- GPU レール（MAX77621 @0x1c）: 最小 618.75mV、最大 1150mV、初期 1000mV、EN は MAX77620 GPIO6。`BOOT_GPU_UV_B1` も 1.0V。→ ドライバの 1.000V(460.8MHz)と 1.100V(768MHz)は上限 1150mV 以内で、`t210_clock.c` は 1.1V 超を拒否している。
- Erista の GPU 最大周波数は 921.6MHz（DTS の `nvidia,gpu-max-freq-khz`）。768MHz のオーバークロック設定はそれ以下。
- B1 の PLL 範囲: 128〜2600MHz、VCO 1300〜2600MHz、ロックのタイムアウト 500。ドライバのプロファイル（N=48/PL=2 → 460.8MHz、N=40/PL=1 → 768MHz、VCO 1.84/1.536GHz）は範囲内。
- 速度別(CVB)の電圧表は NVIDIA の tegra21 dvfs コード側にあり、取得できたリポジトリには無い。固定値は保守的な仮値のままで、**CVB 表は未照合**。

### ruje0504-2/mesa-switch-woa（tegra-x1-wddm, 80dfad4）
- チップID: 0x12b = GM20B(Erista)、0x12e = GM20B_B(Mariko)。同じ `identity.h`（こちらはコメントのみ追記）で、Erista 側は 0x12b だけを電源/クロック対象にしている。ruje 側の `t214_clock.c` は 0x12e のみ受け付ける NA-PLL 経路で、こちらの legacy GPCPLL 経路とは分かれている。
- ABI: `TG_GPU_ADAPTER_ABI`/`ALLOCATION_ABI`/`SUBMIT_ABI` は共に 1。アダプタ ABI は同一。ruje 側の最新はその上に表示プライマリ/リニア面/リソースの構造体と `TG_GPU_QMD_*` を追加している（旧版は引き続き受理される作り）。取り込んだ版はそれらが無い古い切り出しなので、UMD(NVK)を ruje 側の最新に合わせる場合は ABI ヘッダを同期する必要がある。今回は未同期。
- ruje 側の `dvfs.c` は Mariko(HighOPT)用で Erista には適用しない。

## 未確認のこと
- 実機未検証。WDK でのビルドも未実施（ホスト側のモデルテストのみ）。
- NVIDIA ファームウェア 9 ファイルはリポジトリに含まれない。
- 速度別 CVB 電圧表、ruje 最新 ABI への同期（上記）。
- Lite / Mariko には影響しない（DSDT の変更はすべて `SWITCH_ERISTA_GPU` 内）。
