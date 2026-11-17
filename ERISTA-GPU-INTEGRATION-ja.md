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


## 追加検証と修正（hekate / Switchroot L4T / mainline Linux / Microsoft 資料との突き合わせ）

### 修正したもの（実装を変更）
1. **GPU 電圧を固定値から CVB 計算へ**。固定の 460.8MHz@1.000V / 768MHz@1.100V は、nouveau `volt/gm20b.c` の Erista 係数で計算すると、速度ビン（speedo）が低いチップで電圧不足になる。
   460.8MHz@1.0V は speedo 1650 未満、768MHz@1.1V は speedo 1900 未満で不足し、768MHz は speedo 1750 未満ではレール上限 1.150V でも足りない（計算表は下）。
   `tg_gpu_clock_required_uv()` を、fuse（`FUSE_CPU_SPEEDO_2` のシャドウ 0x7000F930）から読んだ speedo と CVB 式で決める実装に変更した。下限 950mV、上限 1150mV。
   fuse が 0 / 読めない、プロファイルが未対応、必要電圧が上限超過の場合は、PMIC にも PLL にも書かずに `RAIL_ERROR` で拒否する。
   speedo の換算は fuse リビジョンで 3 通りあり、リビジョンは読んでいない。電圧は speedo が高いほど下がるので、3 通りのうち**最小値**を使う（必要電圧が最大になる安全側）。
2. **電源投入直後のレール電圧を 0.8V → 1.0V**。Switchroot nvgpu `clk_gm20b.c` では Erista（B1）が `BOOT_GPU_UV_B1 = 1.0V`、Mariko（C1）が 0.8V。Lite（C1）の値が残っていた。dtsi の vdd-gpu 初期値（1.0V）とも一致する。

### 必要電圧の計算（mV、950mV 下限の適用前。係数: nouveau gm20b_cvb_coef）
| speedo | 307.2MHz | 460.8MHz | 768.0MHz | 921.6MHz |
|---|---|---|---|---|
| 1500 | 976 | 1064 | 1286 | 1419 |
| 1700 | 898 | 978 | 1185 | 1310 |
| 1900 | 832 | 906 | 1097 | 1215 |
| 2100 | 780 | 846 | 1023 | 1132 |

### 確認だけして変更しなかったもの
- ACPI のメモリ窓: `cpp` で DSDT を展開した 5 構成で、GPU0 を含むデバイス間の重なりは 0 件。
- I2C 接続: 0x1C（GPU0 のみ Exclusive）、0x3C（GPU0 と PWRB が Shared）。共有接続は Microsoft の SPB 仕様（複数クライアントが別々の論理接続を開ける）の範囲内。
- 接続ロック（`IOCTL_SPB_LOCK_CONNECTION`）は使っていない。GPU 側が 0x3C で書くのは GPIO6 CNFG と AME_GPIO で、電源ボタン側のレジスタとは別。I2C の 1 転送は不可分なので実害は低いが、同じレジスタを触る相手が増えたら追加する。
- 元の DSDT に I2C1 0x4C（温度センサー）の CPUC=Shared / FAN0=Exclusive の組み合わせがある（今回の変更とは無関係。Windows 上の影響は未確認）。
- チップ ID は Erista 経路で 0x12b、SM 53、クラス 0xb197 に統一されている。

### 未確認のもの
- 実機での動作、WDK でのコンパイル、GitHub Actions の実行。
- speedo のリビジョン別換算の実際の値（Erista の個体で、3 通りのどれが正しいか）。
- 電源投入から PLL 起動までの間の動作（1.0V・PLLP 204MHz 由来のクロックで GPU レジスタを読む）。
