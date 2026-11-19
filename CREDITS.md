# クレジット

このリポジトリは多くの先行プロジェクトの成果の上にある。**ここに載っている全員の著作権と許諾条件を尊重すること。**
抜けや誤りに気づいたら Issue / PR で直してほしい。

## ソース由来（コードまたは仕様の出典）
| 出典 | 作者・権利者 | 使った部分 | ライセンス |
|---|---|---|---|
| [winx / NintendoSwitchPkg fork](https://github.com/steck0714/winx) の `NOTICE.winx.md`（winx 側のファイル。本リポジトリには未同梱） | steck0714 ほか。ドライバ群の原作は hayato41200、ruje0504-2、CTCaer など（`drivers/*/LICENSE`・`NOTICE.winx.md` に従う） | `drivers/t210-icosa`・`drivers/t214-hoag` の全ドライバ、ACPI(DSDT)、kit、app | 各ディレクトリの `LICENSE` |
| [hayato41200/Windows-for-Switch(-Drivers)](https://github.com/hayato41200) | hayato41200 | Wi-Fi(BCM4356)・Bluetooth・Audio・Battery 等のベース | 同梱 `LICENSE` |
| [ruje0504-2/Windows-for-Switch-Drivers](https://github.com/ruje0504-2/Windows-for-Switch-Drivers) / [mesa-switch-woa](https://github.com/ruje0504-2/mesa-switch-woa) | ruje0504-2 | T214 Wi-Fi 共通コア、TegraXusb、TegraGpu(KMD)、Windows 向け NVK backend、KMT ABI | ファイルごとの SPDX 表記に従う（Mesa 由来は MIT、T214 電源実装と KMDF probe は GPL-2.0-only。`drivers/*/TegraGpu/NOTICE`） |
| [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch)（`0ff52079`）、[Mesa / NVK](https://docs.mesa3d.org/drivers/nvk.html) | danfromtico、Mesa・NVK 開発者 | GPU コマンドエンコーダ、シェーダ(NAK)・NVK の GM20B 対応。ホストテストの検証元 | MIT |
| [DanilKhmyrov/windows-switch](https://github.com/DanilKhmyrov/windows-switch) | DanilKhmyrov | 電源ボタン長押し→Ctrl+Alt+Del（swhid）の挙動、swbatt、touchread、tune.ps1（`reference/windows-switch/`） | GPL-2.0（同梱 `LICENSE`） |
| SwitchDrivers（2026.11.x 各版）、`windows-10-switch-arm64-*` | 各配布者 | 段階0スクリプト、manifest、ドライバ構成の突き合わせ（`reference/switchdrivers-*`） | 各配布物に従う |
| [CTCaer/hekate](https://github.com/CTCaer/hekate) | CTCaer、naehrwert、shchmue、Billy Laws ほか | MAX77620/MAX77621/BM92T36/BQ24193/MAX17050 のレジスタ・シーケンス、I2C、Joy-Con、タッチ、ALS、GPU 電源順（`reference/l4t/hekate/`） | GPL-2.0 |
| [CTCaer/switch-l4t-kernel-nvgpu](https://github.com/CTCaer/switch-l4t-kernel-nvgpu)（Switchroot の nvgpu） | NVIDIA CORPORATION、CTCaer、Switchroot | GM20B の GPCPLL(`clk_gm20b.c`)・レジスタ定義（`reference/l4t/nvgpu/`）。Erista GPU の PLL ロック手順の修正根拠 | MIT |
| [CTCaer/switch-l4t-platform-t210-nx](https://github.com/CTCaer/switch-l4t-platform-t210-nx) | Billy Laws、CTCaer | PMIC・電源ツリー、GPU レール(MAX77621 0x1C、618.75〜1150mV、初期 1000mV)、DVFS 上限（`reference/l4t/platform-t210-nx/`） | GPL-2.0 |
| [Switchroot](https://switchroot.org) / L4T / LineageOS for Switch | Switchroot チーム、NVIDIA | Linux/Android 側の Switch ハードウェア対応という全体の背景、ブート連鎖 | 各プロジェクトに従う |
| EDK II / NintendoSwitchPkg | TianoCore、各ポート作者 | UEFI 側（本リポジトリには含めない。ACPI の前提） | BSD-2-Clause-Patent ほか |
| Linux nouveau v6.12（`nvkm/subdev/mmu/vmmgf100.c` ほか）| Red Hat Inc. 2017、nouveau 開発者、NVIDIA | GM20B のページテーブル形式（`core/gm20b_mmu.c`）、PMU 手順の参照。許諾文は `drivers/*/TegraGpu/NOTICE` に全文 | MIT |

## Erista GPU（TegraGpu / SWOA0F02）で具体的に参照したファイルと値
どの値がどの上流のどこに対応するか。いずれも `reference/l4t/` に同梱（固定コミットは `reference/l4t/SOURCES.md`）。

| 使った値・手順 | 参照ファイル | 上流・固定コミット | 権利者 / ライセンス |
|---|---|---|---|
| GPU レール = MAX77621（I2C5 0x1C）、EN ピン = MAX77620 GPIO6、電圧 = 606.25mV + 6.25mV × code、VOUT/VOUT_DVS の書き順、CONTROL1=0xB0 / CONTROL2=0xC1（HOS 既定）、GPIO6 を push-pull・出力・High（0x09）にする設定、AME_GPIO bit6 | `hekate/bdk/power/max7762x.[ch]`、`max77620.h` | CTCaer/hekate `8e1392df4569ac501a115867056b13c361429e9e` | CTCaer、naehrwert、shchmue、Billy Laws ほか / GPL-2.0 |
| HIDREV 判定（chip 0x21、major 1 = T210、major 2 = T210B01 を拒否） | `hekate/bdk/soc/pmc.h`、`bdk/power/max7762x.h` | 同上 | 同上 |
| GPU レールの仕様の裏取り（0x1C、EN = max77620 の GPIO 6、最小 618.75mV、最大 1150mV、初期 1000mV、enable ramp 500µs、active-discharge / falling-slew / remote-sense） | `platform-t210-nx/kernel-dts/nx-platforms/tegra210-odin-pmic.dtsi`（`gpu_max77621_reg`） | CTCaer/switch-l4t-platform-t210-nx `cf785c4c176499b301170d79fe57b77f365b73cd` | Billy Laws、CTCaer / GPL-2.0 |
| GPCPLL（B1 = Erista の旧方式）: VCO 1300〜2600MHz、M 1〜255、N 8〜255、PL 1〜31（除数 = PL）、gpc2clk = 38.4MHz × N / PL、GPCCLK はその 1/2、ロック待ち 500+1、ロック検出 bit4 の扱い、bypass → IDDQ 解除 → COEFF → 有効化 → ロック → SYNC → SEL_VCO の順、出力分周・SVOP・slowdown の初期化、クロックカウンタ（800 参照クロック計数）、BOOT_GPU_UV_B1 = 1.0V | `nvgpu/gm20b/clk_gm20b.[ch]`、`nvgpu/hw-gm20b/hw_trim_gm20b.h` | CTCaer/switch-l4t-kernel-nvgpu `1ae0167d360287ca78f5a2572f0de42594140312` | NVIDIA CORPORATION、CTCaer、Switchroot / MIT |
| GPU 電源の投入順（レール → クロック → リセット → クランプ解除 → リセット解除） | `nvgpu/os-linux/platform_gk20a_tegra.[ch]` | 同上 | NVIDIA CORPORATION / GPL-2.0 |
| GPU 電圧 = 速度ビン（speedo）と周波数から決める式 `((c2·s/100 + c1)·s/100 + c0)`、Erista(GM20B) の CVB 係数（460.8MHz = {2122872, -95095, 1632}、768MHz = {2458841, -102685, 1632}）、電圧の下限 950mV（`speedo_to_vmin`）、µV を mV に切り上げる丸め | `linux-mainline/nouveau-volt/gm20b.c`、`gk20a.c` | torvalds/linux `666539eecdf4db623f5e3c3e55cf9f2dfad24aa4` | NVIDIA CORPORATION / MIT |
| GPU speedo の fuse 位置（`FUSE_CPU_SPEEDO_2`、シャドウ 0x7000F930）と、リビジョン別の換算式 | `linux-mainline/tegra-fuse/speedo-tegra210.c` | 同上 | NVIDIA CORPORATION / GPL-2.0-only |
| GM20B と GM20B_B が同じ HAL であること（Lite 由来の共通コアをそのまま使える根拠）、GR / FIFO / MMU / PMU / ACR の手順 | `nvgpu/gm20b/hal_gm20b.c`、`gr_gm20b.c`、`fifo_gm20b.c`、`mm_gm20b.c`、`pmu_gm20b.c`、`acr_gm20b.c` | 同上 | 同上 |
| GPU コマンドエンコーダ（ホストテストの検証元） | `src/nouveau/horizon/`（本リポジトリには含めず、CI で取得） | danfromtico/mesa-switch `0ff52079ca9e9e837ed27eb95a8d239f439a1864` | danfromtico、Mesa・NVK 開発者 / MIT |
| Lite 版 TegraGpu の構造（電源・クロック・計数・巻き戻しのロジック、Windows 側シェル、ACPI リソース構成） | `drivers/t214-hoag/TegraGpu/` | ruje0504-2/Windows-for-Switch-Drivers、ruje0504-2/mesa-switch-woa | ruje0504-2 / ファイルごとの SPDX（電源実装と KMDF probe は GPL-2.0-only） |
| Lite 版 SwitchFan（ファン・CPU・メモリ(EMC)制御、GPU の PMIC ブリッジ。Fan33 = `DriverVersion` 33） | `drivers/t214-hoag/SwitchFan/`（`drivers/SwitchFan` のコミット `c5ae935`） | ruje0504-2/Windows-for-Switch-Drivers `c5ae935b59c6229db4be23c561d31088503f40e4`。GPU ブリッジの実体 `t214/gpu_pmic.h`・`t214/emc_gpu_vote.h`・`t214/host1x_clock.h` は ruje0504-2/mesa-switch-woa `c77f8a61fd4b627b218fee1db34b654b4e7233a2` の `tegra-x1-wddm/integration/switch-lite/windows/SwitchFan/`（本リポジトリには含めず、CI で取得） | ruje0504-2 ほか / ファイルごとの SPDX（無いファイルは `drivers/t214-hoag/LICENSE` の GPL-2.0。`switchfan.c` は先頭に W4T-CREDIT ブロック） |

**電圧**: 速度別（CVB）の表を上記の出典から取り入れ、チップごとの speedo で計算する（固定値は廃止）。下限 950mV は nouveau の `speedo_to_vmin` の最大値、上限 1150mV は dtsi の `regulator-max-microvolt`。speedo の換算（fuse リビジョン別）は不確定なため、3 通りのうち最小値を使う安全側の扱い。

**コードの扱い**: 上記の値・手順は参照して、本リポジトリの実装として書き直したもの。NVIDIA の GPU ファームウェア（`NETB_img.bin` など 9 ファイル）は含めない。

## 公式資料（仕様の根拠。コードは含まない）
- Microsoft Learn: [KMDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/)、[SpbCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/spb/)、[GpioClx](https://learn.microsoft.com/en-us/windows-hardware/drivers/gpio/)、[VHF](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)、[WiFiCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/writing-a-wificx-client-driver)、[WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)、[Modern Standby のプラットフォーム設計](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/platform-design-for-modern-standby)、[PEP](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/platform-extension-plug-ins--peps-)
- NVIDIA / Maxim / TI / Broadcom の公開データシート・公開ソース（各ドライバの README に個別記載）

## ソース先頭のクレジット表示(2026-11-18 追加)
- `drivers/t210-icosa`・`drivers/t214-hoag` の `src` 等にある `.c/.cpp/.h` のうち、SPDX・Copyright の表示が無かった 39 ファイルに、先頭へクレジットブロック(`W4T-CREDIT-BEGIN`〜`END`)を足した。
  Erista 版は hayato41200/Windows-for-Switch-Drivers 由来(ruje0504-2 の古いコミットと一致、`docs/HAYATO-DRIVER-AUDIT-ja.md`)、Lite 版は ruje0504-2 の系統で共有実装は hayato41200 由来(`drivers/t214-hoag/README.md`)。
  **上流のファイルに個人の作者・著作権の行は無いので、ファイル単位の作者は書いていない**(分かっていないものを作らない)。もともと SPDX・Copyright があるファイル(Mesa/NVIDIA/ruje0504-2 等)には触れていない。
- `drivers/t214-aula` の `.c` 4 本は、上の表の DanilKhmyrov/windows-switch を出典・所有者として明記(`layout.json` で上流とのバイト一致を検証)。
- ファイル単位の作者が分かる人は PR でブロックを直してほしい。

## 本リポジトリでの追加作業
- 2026-11-18: OLED(Aula)枠の `swhid` / `swbatt` / `touchread` / `swagentsvc` と INF 3 本を、t210-icosa / t214-hoag と同じ「ドライバごとのフォルダ」(`drivers/t214-aula/drivers/`)へ移動。`.c` の先頭に出典・作者(DanilKhmyrov)のクレジットブロックを追加(`layout.json`・`tools/aula_layout.py` で上流とのバイト一致を検証)（Claude による作業）。
- OLED(Aula)枠 `drivers/t214-aula/` の新設（windows-switch v0.8.0 のテキスト部分と SHA-256 一覧、`main` のソース一式 `upstream-src/`）、`tools/fwfetch/`（実機からの取り出し・照合ツール。2026-11-18 に Rust で作り直し）と混入検査 `tools/check_no_proprietary.py`（Claude による作業）。
- `build/`（型番別のビルド済み置き場）、`tools/build/`、`build-fwfetch.yml`、`build-all-drivers.yml` の組み立て・コミット段（Claude による作業。2026-11-18、ユーザー steck0714 の指示）。fwfetch の Rust 依存は `tools/fwfetch/THIRD-PARTY.md`。
- 2026-11-19: `drivers/t214-hoag/SwitchFan` を、旧版(DriverVersion 13)から ruje0504-2/Windows-for-Switch-Drivers `c5ae935`(DriverVersion 33)へ差し替え。`switchfan.c` の先頭クレジットブロックは新しい出典に更新(旧版にあった CPU 目標 1497.6 MHz の `CPU_TARGET_KHZ` は上流の v33 にも含まれている)。GPU ブリッジ用ヘッダは mesa-switch-woa のコミット固定で CI が取得する（Claude による作業。ユーザー steck0714 の指示）。
- 2026-11-19: OLED(Aula)向けに `tools/check_swbatt_bridge.py` を追加し、`drivers/t214-aula/README.md` と `build-aula-gpu.yml` の説明を更新(swbatt のブリッジ ABI と mesa-switch-woa `c77f8a6` の対応を確認。`swbatt.c` は変更していない)（Claude による作業。ユーザー steck0714 の指示）。
- 統合・整理、HWID/DSDT 静的検査、windows-switch 互換メモ、電源ボタン長押し(HoldSasMs)の移植、Erista GPU の取り込みと GPCPLL ロック検出修正、電源コールバック検査（Claude による作業。ユーザー steck0714 の指示と所有物に基づく）。

- 2026-11-19: ファームウェアを「ビルドに入れず、あとから読み込む」方式に変更。`tools/fwfetch/`(利用者自身の SD からファームウェアを取り出して照合・配置するツール)は、Switchroot(L4T / Android 系)のファームウェア取得の仕組みが元(Switchroot: https://gitlab.com/switchroot 、ミラー https://github.com/switchroot 。単体のリポジトリは無く、どのリポジトリのどのスクリプトかは未特定。ユーザーの申告に基づく)。このリポジトリの Rust 版への書き直しと拡張は steck0714 ([steck0714/W4T-Windows-Drivers](https://github.com/steck0714/W4T-Windows-Drivers))。`fwfetch --deploy-drivers`、GPU プローブ/レンダー INF からのファームウェア同梱の除去、Erista/Lite の GPU ワークフローで `.sys` を常にビルド・署名する変更、起動時タスクの `-DriverDir` 対応を追加(Claude による作業、ユーザー steck0714 の指示)。

## 含めないもの
Broadcom ファームウェア、NVIDIA の GPU / XUSB ファームウェア、`bl31.bin`、秘密鍵。ファームウェアは `tools/fwfetch/` が利用者の実機（Switchroot を入れた SD）から取り出す。ビルド済みの `.sys` / `.exe` は 2026-11-18 から `build/` に置いてよい。

## 2026-11-18 追加（スリープ / HDMI の参照）
| 参照 | 出典 | ライセンス |
|---|---|---|
| `reference/atf/`（`plat_psci_handlers.c`, `tegra_def.h`） | CTCaer/switch-l4t-atf `ed2ef6be`（ARM Limited / NVIDIA / CTCaer） | BSD-3-Clause |
| `reference/nvidia-dc/dp.c`、`SwitchDisplay/src/dp_link.h`（移植） | CTCaer/switch-l4t-kernel-nvidia `76e6d489`（NVIDIA CORPORATION / CTCaer） | GPL-2.0 |
| `SwitchSleep` / `lpi.asl.inc` の根拠 | hekate `bootloader/l4t/l4t.c` `8e1392df`（CTCaer）、Switchroot NX DT（Billy Laws / CTCaer） | GPL-2.0 |
| `reference/nvidia-dc/`（`dc` 全体） | CTCaer/switch-l4t-kernel-nvidia `76e6d489`（NVIDIA CORPORATION / CTCaer） | GPL-2.0 |
| `reference/hekate-l4t/`、`reference/atf/plat-nvidia-tegra/` | CTCaer/hekate `8e1392df`、CTCaer/switch-l4t-atf `ed2ef6be` | GPL-2.0 / BSD-3-Clause |
| `reference/ms-pep/` | microsoft/Windows-driver-samples `d49bb7d7`（Microsoft） | MS-PL |

## 2026-11-19 追加（README のクレジット整理で見つかった抜け）
| 参照 | 出典 | ライセンス |
|---|---|---|
| GPU 電圧表 GPUB01_NA_CVB_TABLE（`drivers/soc/tegra/tegra210-dvfs.c`）、Wi-Fi / PCIe の参照 | [CTCaer/switch-l4t-kernel-4.9](https://github.com/CTCaer/switch-l4t-kernel-4.9) `2d0059fd3167a8df756de2aa0489d4aa70a9fc15`（NVIDIA CORPORATION、CTCaer、Switchroot）。`drivers/*/TegraGpu/NOTICE` | GPL-2.0 |
| GM20B レジスタ定義 `hw_*_gm20b.h`（無改変、`drivers/t210-icosa/gpu/GM20B-Diagnostic/regs/nvgpu/`） | [OE4T/linux-nvgpu](https://github.com/OE4T/linux-nvgpu) `l4t/l4t-r32.7.6` `7fbefcbf4b39643ab414454e6e4154865b333757`（NVIDIA CORPORATION） | MIT |
| UEFI 側のパッチ（`drivers/t214-aula/upstream-src/patches/`） | [imbushuo/NintendoSwitchPkg](https://github.com/imbushuo/NintendoSwitchPkg) `09c3ade4`、[tianocore/edk2](https://github.com/tianocore/edk2)（Intel Corporation の `LaffStd.c` 由来フォント、ARM Limited / Linaro / Red Hat / Andrei Warkentin / Bingxing Wang、naehrwert）。`drivers/t214-aula/NOTICE.md` | BSD-2-Clause-Patent ほか |
| WDDM 割り当ての参照 | [microsoft/graphics-driver-samples](https://github.com/microsoft/graphics-driver-samples) `de4a2161`（Microsoft） | MIT |
| KMDF / NetAdapterCx の参照（コードなし） | [microsoft/Windows-Driver-Frameworks](https://github.com/microsoft/Windows-Driver-Frameworks)、[microsoft/Network-Adapter-Class-Extension](https://github.com/microsoft/Network-Adapter-Class-Extension)（Microsoft） | MIT |
| SwitchWOA の ACPI 前提、Vulkan 進捗 | [ruje0504-2/winx](https://github.com/ruje0504-2/winx)（ruje0504-2） | 各リポジトリに従う |
| OLED 上流のビルド時取得物（本リポジトリには含めない） | [mstorsjo/llvm-mingw](https://github.com/mstorsjo/llvm-mingw)、[KhronosGroup/Vulkan-Headers・Vulkan-Loader](https://github.com/KhronosGroup)、[PowerShell/Win32-OpenSSH](https://github.com/PowerShell/Win32-OpenSSH) | 各自のライセンス |
