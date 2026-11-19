# W4T-Windows-drivers

(Tegra X1) 上の Windows ARM64 で動いているドライバを集め、**WinX の ACPI(SWOA 系 ID)に合わせて**
まとめたリポジトリ。Erista(T210/Icosa)、Lite(T214/Hoag)、**OLED(T214/Aula)を別枠**で扱う(Iowa は専用の枠なし)。

> **未実機。** 確認したのはソース/INF/DSDT の突き合わせと、
> ホストで動く単体テスト。あとビルドが出来るか、だけ。実機で動く証明ではない。

## 構成

| パス | 中身 |
|---|---|
| `drivers/t210-icosa/` | Erista 用 WinX ドライバのソース・INF・kit（winx `usually` からの複製） |
| `drivers/t214-hoag/` | Lite 用（Wi-Fi 共通コア、TegraGpu、TegraXusb を含む） |
| `drivers/t214-aula/` | **OLED(HEG-001)用。windows-switch v0.8.0 の構成**（実機報告は配布者 1 台のみ）。テキストの kit、上流ソース一式（`upstream-src/`、未ビルド。ライセンスはファイルごとに `LICENSE-AUDIT.md`、帰属は `NOTICE.md`）、全ファイルの SHA-256 一覧。バイナリは `build/HEG-001/` に `tools/build/fill_heg001.py` で置く |
| `build/` | **型番別のビルド済みドライバ置き場**（`HAC-001` / `HAC-001-01` / `HDH-001` / `HEG-001`）。いまは置き場と手順だけで `.sys` は未作成（`build/README.md`） |
| `tools/fwfetch/` | **再配布できない NVIDIA / Broadcom / Switchroot ファイルを自分の SD から取り出して照合・配置するツール（Rust 製、`unsafe` 禁止、読み取り専用、リポジトリ外に出力）** |
| `tools/build/` | `build/` を埋めるスクリプト（`assemble_build.py` = CI の成果物、`fill_heg001.py` = 上流 zip をハッシュ照合して配置）とそのテスト |
| `acpi/Dsdt.winx-usually.asl` | INF の HWID と突き合わせる基準 DSDT |
| `reference/windows-switch/` | windows-switch の swbatt / swhid / touchread / tune.ps1（参考、GPL-2.0）。移植元 |
| `reference/windows-for-switch-drivers/` | ruje0504-2/Windows-for-Switch-Drivers `c5ae935` の `core_voltage.h` 1 ファイル（MIT）。SwitchFan の `emc_windows.h` が使う IOCTL 契約ヘッダの上流コピー |
| `reference/switchdrivers-*` | SwitchDrivers の manifest・段階0スクリプト |
| `docs/DRIVER-INVENTORY.md` | **全ドライバ一覧**（HWID・版・minUefi・系統・注意点） |
| `docs/WINDOWS-SWITCH-COMPAT.md` | windows-switch との互換性、併用禁止の組み合わせ |
| `docs/driver-gap-audit-ja.md`, `docs/HAYATO-DRIVER-AUDIT-ja.md` | winx 側の不足点検と hayato/ruje 突き合わせ |
| `tools/check_hwid_matrix.py` | INF の HWID が DSDT に存在するか検査（t210/t214 全 INF: OK） |
| `tools/run_host_tests.sh` | ホストで動くテスト一式 |
| `tools/check_power_callbacks.py` | 各 KMDF ドライバの D0Entry/D0Exit/S0Idle/Sx ウェイク/SelfManagedIo の有無と、「ポーリングタイマーを Dx で止めていない」ドライバを一覧 |
| `tools/check_no_proprietary.py` | 再配布不可ファイルがリポジトリに紛れ込んでいないか（ハッシュ・名前）を検査 |
| `tools/check_abi_sync.py` | TegraGpu の ABI ヘッダが ruje0504-2/mesa-switch-woa(80dfad4)と同一か + 単独コンパイル |
| `tools/ci/` | **GitHub 上で全 Erista ドライバをビルド・署名・キット化**（`Build-AllDrivers.ps1` + `drivers-t210.json`） |
| `COPYING.md`, `LICENSES/` | ライセンスの案内と SPDX 別の全文 |
| `CREDITS.md`, `reference/l4t/` | 出典・クレジットと、L4T/hekate/Switchroot の参照ソース |

## 2026-11-19 追加分: HEG-001(OLED)を他の型番と同じ CI 方式に
- `build/models.json` の HEG-001 を `ci` に変更。`tools/ci/drivers-t214-aula.json` で swhid / swbatt / switchbt / switchaudio / switchwifi をビルド・署名し、`build-all-drivers.yml` が `swagentsvc.exe`(ARM64)と windows-switch の `agent\` スクリプトを足して `build/HEG-001/drivers/agent/` を作る。上流バイナリの照合は `heg001_upstream = true` で従来どおり。
- `tools/ci/verify_build.py` に `--no-inf-dir` / `--no-cat-dir`(INF なしの swhid / swbatt、カタログを Switch 上で作る Wi-Fi/BT/音声用)を追加。
- fwfetch: HEG-001 は起動タスクでは配置されない(GPU 以外)ので、成果物の README に `--deploy <起動パーティション>` の手順を入れた。
- **どれも CI 未実行・WDK 未確認・実機未確認。** v0.8.0 のバイナリとはバイト一致しない。
- **テスト署名キット**: 全型番の `build/<型番>/test-signing/` に、その実行の `SwitchWOA-Test.cer` と `Install-TestSigning.cmd` ほかを同梱（`tools/testsign/`、`tools/build/make_testsign_kit.py`）。Lite の Stage0 に残っていた古い `.cer` も実行ごとの証明書で上書き。GPU の確認は `docs/ERISTA-GPU-CHECK-ja.md`、ビルドの確認は `docs/FULL-BUILD-CHECK-ja.md`。

## 2026-11-18 追加分（4 回目: スリープ→HDMI）
- **スリープ**: `acpi/t210/lpi.asl.inc`（CPU `_LPI`、`SWITCH_LPI` で opt-in。コア停止は Switchroot DT が c7 を無効にしている根拠で無効フラグ付き）、`SwitchSleep/`（PSCI 状態 ID と SC7 前提検査。ATF `ed2ef6be` と突き合わせ）。`tools/check_dsdt_compile.py` が iasl で 3 構成をコンパイル。→ `docs/SLEEP-DESIGN-ja.md`
- **HDMI**: `SwitchDisplay/src/dp_link.h`（L4T `tegra_dc_dp_calc_config` の移植。原本との差分テスト 2160 通り一致）。→ `docs/HDMI-DESIGN-ja.md`
- 再開用の進捗は `PROGRESS.md`。**どれも WDK ビルド・Windows・実機は未確認。**

## 2026-11-18 追加分（3 回目）
- **ビルド済みバイナリを置いてよい方針に変更**（作者の許可）。置く場所は `build/<型番>/`。再配布できないファームウェアと秘密鍵は引き続き不可。`.gitignore`・`COPYING.md`・`tools/check_no_proprietary.py` を合わせた。
- **`build/` を型番別に新設**: `HAC-001`（Erista）は CI でビルドして置く設定まで。`HAC-001-01`（Iowa）はドライバ一式が無いので空。`HDH-001`（Lite）は `tools/ci/drivers-t214-hoag.json` で CI 化（未実行）。`HEG-001`（OLED）は上流のビルド済みをハッシュ照合して置く。**このリポジトリを作った環境には WDK も Windows も無く、`.sys` は 1 つも作れていない。**
- **`build-all-drivers.yml`**: 全 Erista ドライバのビルド後に `build/HAC-001` を組み立てて成果物にする。手動実行で `commit_build` を ON にすると、そのブランチへコミットする。
- **fwfetch を Rust で作り直し**（`tools/fwfetch/`、旧 PowerShell/C# 版は `legacy/`）。実行ファイル 1 つ、マニフェスト埋め込み、`unsafe` 禁止、全オフセットの範囲検査、伸長量の上限、出力先の検証、配置前のハッシュ再検証。合成データのテスト 30 件（破損イメージ 210 件を含む）は Linux で通過。**Windows では未ビルド・未実行**。`build-fwfetch.yml` が x86_64 / ARM64 の `.exe` を作る。

## この版で足した変更（前回まで）

- **OLED(Aula)を Lite から分離**: `drivers/t214-aula/` を新設。OLED で動作報告のあるもの（windows-switch v0.8.0）だけを置き、Lite 側の設定を流用しないルールを明記。
- **再配布できないファイルは実機から取る**: `tools/fwfetch/`（2026-11-18 から Rust 版。旧 `Get-SwitchFirmware.ps1` は `legacy/`）。Switchroot の SD から `bl31.bin`、initramfs 内の XUSB ファーム、ext4 上の BCM4356 ファーム/パッチ、GPU ファームを探し、公開ハッシュがあれば照合して `--out-dir`（git 外）に出す。**実イメージ・実機・Windows では未確認**。

- **t210 SwitchPowerButton 1.0.0.2**: windows-switch の swhid にあった「電源ボタン長押しで Ctrl+Alt+Del」を
  SWOA0601 に移植。レジストリ `HoldSasMs` で有効化（既定は無効で、従来と同じ動作）。→ `drivers/t210-icosa/drivers/SwitchPowerButton/README.md`

- Erista GPU（TegraGpu / ACPI\SWOA0F02）を取り込み、GPCPLL のロック検出と待ち時間を修正。詳細は `docs/ERISTA-GPU-INTEGRATION-ja.md`（実機未検証）。

- **ルートに `CREDITS.md`**（出典・作者・ライセンスの一覧）と、**L4T / hekate / Switchroot の参照ソース同梱**（`reference/l4t/`、各ファイルのライセンス表記そのまま。固定コミットは `reference/l4t/SOURCES.md`）。
- **TegraGpu(Erista)の ABI ヘッダ 3 本を ruje0504-2/mesa-switch-woa(80dfad4)に同期**（`adapter_abi.h` / `allocation_abi.h` / `submit_abi.h`）。T214 側は据え置き。
- **電源コールバック検査** `tools/check_power_callbacks.py` と、スリープ・HDMI・不足ドライバの現状 → `docs/SLEEP-AND-MISSING-DRIVERS-ja.md`。
- 静的監査メモ: `docs/audit-2026-11-17-static.md`（Tegra210I2c の長い stall を修正）。
- `build-switchaudio.yml` は、全ドライバを 1 つの証明書でビルドする **`build-all-drivers.yml`** に統合（Audio を含む。未実行）。

## 2026-11-17 追加分（2 回目）
- **スリープ(Dx)対応(T210)**: Fan / Battery / Sensor / CpuClock / PowerButton / Touch に `EvtDeviceSelfManagedIoSuspend/Restart` を追加。ポーリング用タイマーを止めて I2C を叩かないようにし、復帰時に再開。PowerButton は「起こした電源ボタン押下」をもう一度押下として扱わない（即スリープに戻るのを防ぐ）。Sensor は ALS/IMU を停止・再初期化、CpuClock は停止時に既定クロックへ戻し復帰時に要求プロファイルを再適用、Fan は停止時に UEFI 既定の 76 へ。
- **全タイマーの再アーム競合を修正**: コールバックが「`Stopping` を確認 → 再アーム」する間に停止処理が入ると、止めたはずのタイマーが復活していた。再アーム後に `Stopping` を再確認して取り消す。
- **Battery**: `SelfManagedIoInit` が毎回 Gauge/Charger を開き直してハンドルを漏らしていたのを修正し、Cleanup で閉じる。
- **Bluetooth(T210) 1.0.0.15 → 1.0.0.19**: t214 のソースへ更新（パッチの全体検証、`BootPending` が毎回 BT を無効にする不具合、UART-D の CAR 復元）。windows-switch 0.8.0 同梱の BT と同じ版。
- **ビルド**: `Build-Driver.ps1` を 1 本に統合（C++ / WiFiCx 対応、signtool/Inf2Cat の自動検出、`-NoWarnAsError`、`-CertThumbprint`＝シークレットの PFX を実際に使う）。`build-all-drivers.yml`: ホストテスト + 全ドライバのビルドと署名 + `v*` タグで**プレリリース**作成。
- **BCD を開けない問題**(`bcdedit` が「システム デバイスが見つかりません」): `Stage0-SwitchSetup.ps1 -Mode Prepare` が BCD にシステムストアの印を付ける。起動パーティションが非アクティブな場合は PC から MBR の 446 バイト目を 0x80 に（windows-switch v0.8.0 のリリースノートによる。未実機）。
- **GPU の PMIC I2C 転送を 3 回までリトライ**（`TegraGpu/windows/platform_io.c`）。Erista Wi-Fi の INF を 0.5.7.0 に。
- 詳細: `docs/audit-2026-11-17-static.md`（追補）、`docs/SLEEP-AND-MISSING-DRIVERS-ja.md`（追補）。**どれも WDK でのビルドと実機は未確認。**

## 使い方

    tools/check_hwid_matrix.py        # INF と DSDT の ID を検査
    tools/run_host_tests.sh           # ホスト側テスト（8 件: ID、battery、bluetooth、長押し、L4T 参照、ABI、ACPI、GPU 全モデル）
    tools/ci/Build-AllDrivers.ps1     # Windows で全ドライバをビルド（GitHub Actions も同じものを実行）
    python tools/build/assemble_build.py --model HAC-001 --kit-dir build/t210/kit   # 成果物を build/HAC-001 へ
    cargo build --release --manifest-path tools/fwfetch/Cargo.toml                  # fwfetch

ビルドと署名の手順は各 `kits/` と `drivers/*/README.md`（WDK 10.0.19041 以降、Wi-Fi は 10.0.26100、テスト署名 + `testsigning on`）。

## クレジット（使った全リポジトリと作者）

このリポジトリは下の先行プロジェクトの成果の上にある。**全員の著作権と許諾条件を尊重すること。** 詳細（参照ファイル・固定コミット・値の対応）は `CREDITS.md`、
ファイルごとの許諾は各ディレクトリの `LICENSE` / `NOTICE` / `LICENSE-AUDIT.md`、全文は `LICENSES/`。抜け・誤りは Issue / PR で直してほしい。
ファイル単位の作者が上流に書かれていないものは、**分からないまま作者名を書いていない**（出典のリポジトリ・所有者だけを書いた）。

### 本リポジトリ・直接の土台
| リポジトリ | 作者・権利者 | 使った部分 | ライセンス |
|---|---|---|---|
| [steck0714/W4T-Windows-Drivers](https://github.com/steck0714/W4T-Windows-Drivers) | steck0714（リポジトリ所有者・方針と指示） | 本リポジトリ全体の統合 | GPL-2.0 |
| [steck0714/winx](https://github.com/steck0714/winx)（NintendoSwitchPkg fork、`NOTICE.winx.md`） | steck0714 ほか | Erista / Lite 用ドライバ・ACPI(DSDT)・kit・app の複製元 | 各ディレクトリの `LICENSE` |
| [ruje0504-2/winx](https://github.com/ruje0504-2/winx)（WinX companion firmware） | ruje0504-2 | SWOA 系 ACPI デバイスの前提、Vulkan 進捗ページ（参照のみ） | 各リポジトリに従う |

### Windows ドライバ（ソース由来）
| リポジトリ | 作者・権利者 | 使った部分 | ライセンス |
|---|---|---|---|
| [hayato41200/Windows-for-Switch-Drivers](https://github.com/hayato41200/Windows-for-Switch-Drivers)、[hayato41200/Windows-for-Switch](https://github.com/hayato41200/Windows-for-Switch) | hayato41200 | Wi-Fi(BCM4356)・Bluetooth・Audio・Battery 等、Erista 版ドライバの原作 | 同梱 `LICENSE` |
| [ruje0504-2/Windows-for-Switch-Drivers](https://github.com/ruje0504-2/Windows-for-Switch-Drivers) | ruje0504-2 | T214 Wi-Fi 共通コア、TegraXusb、Lite 版ドライバ一式 | ファイルごとの SPDX |
| [ruje0504-2/mesa-switch-woa](https://github.com/ruje0504-2/mesa-switch-woa) | ruje0504-2 | TegraGpu(KMD)、Windows 向け NVK backend、KMT ABI ヘッダ | Mesa 由来は MIT、T214 電源実装と KMDF probe は GPL-2.0-only |
| [DanilKhmyrov/windows-switch](https://github.com/DanilKhmyrov/windows-switch) | DanilKhmyrov | OLED(Aula)枠一式：swhid / swbatt / touchread / swagentsvc、w64.cmd、tune.ps1、電源ボタン長押しの挙動 | GPL-2.0 |
| SwitchDrivers（2026.11.x 各版）、`windows-10-switch-arm64-*` | 各配布者 | 段階0スクリプト、manifest、構成の突き合わせ | 各配布物に従う |

### GPU（ライブラリ・参照）
| リポジトリ | 作者・権利者 | 使った部分 | ライセンス |
|---|---|---|---|
| [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch)（`0ff52079`）、[steck0714/mesa-switch](https://github.com/steck0714/mesa-switch)（フォーク、参照のみ）、[Mesa / NVK](https://docs.mesa3d.org/drivers/nvk.html) | danfromtico、Mesa・NVK 開発者 | GPU コマンドエンコーダ、NAK / NVK の GM20B 対応、ホストテストの検証元 | MIT |
| [CTCaer/switch-l4t-kernel-nvgpu](https://github.com/CTCaer/switch-l4t-kernel-nvgpu) | NVIDIA CORPORATION、CTCaer、Switchroot | GM20B の GPCPLL(`clk_gm20b.c`)・レジスタ定義、PLL ロック手順 | MIT（`platform_gk20a_tegra` は GPL-2.0） |
| [CTCaer/switch-l4t-kernel-4.9](https://github.com/CTCaer/switch-l4t-kernel-4.9)（`2d0059fd`） | NVIDIA CORPORATION、CTCaer、Switchroot | GPU 電圧表(GPUB01_NA_CVB_TABLE)、Wi-Fi / PCIe 実装の参照 | GPL-2.0 |
| [CTCaer/switch-l4t-platform-t210-nx](https://github.com/CTCaer/switch-l4t-platform-t210-nx) | Billy Laws、CTCaer | PMIC・電源ツリー、GPU レール、DVFS 上限 | GPL-2.0 |
| [OE4T/linux-nvgpu](https://github.com/OE4T/linux-nvgpu)（`l4t/l4t-r32.7.6`、`7fbefcbf`） | NVIDIA CORPORATION、OE4T | GM20B レジスタ定義 `hw_*_gm20b.h`（無改変、`drivers/t210-icosa/gpu/GM20B-Diagnostic/`） | MIT |
| [torvalds/linux](https://github.com/torvalds/linux)（nouveau v6.12、`666539ee`） | Red Hat Inc. 2017、nouveau 開発者、NVIDIA | GM20B ページテーブル形式、PMU 手順、電圧・speedo の式 | MIT / GPL-2.0-only（ファイルごと） |

### ブート・電源・表示（参照ソースの同梱：`reference/`）
| リポジトリ | 作者・権利者 | 使った部分 | ライセンス |
|---|---|---|---|
| [CTCaer/hekate](https://github.com/CTCaer/hekate)（`8e1392df`） | CTCaer、naehrwert、shchmue、Billy Laws ほか | MAX77620/MAX77621/BM92T36/BQ24193/MAX17050 のシーケンス、I2C、Joy-Con、タッチ、ALS、GPU 電源順、`l4t.c`（`reference/l4t/hekate/`） | GPL-2.0 |
| [CTCaer/switch-l4t-atf](https://github.com/CTCaer/switch-l4t-atf)（`ed2ef6be`） | ARM Limited、NVIDIA、CTCaer | PSCI / SC7 の状態 ID（`reference/atf/`、スリープの根拠） | BSD-3-Clause |
| [CTCaer/switch-l4t-kernel-nvidia](https://github.com/CTCaer/switch-l4t-kernel-nvidia)（`76e6d489`） | NVIDIA CORPORATION、CTCaer | DisplayPort / DC（`dp.c` の移植 `dp_link.h`、`reference/nvidia-dc/`） | GPL-2.0 |
| [Switchroot](https://switchroot.org) / L4T / LineageOS for Switch | Switchroot チーム、NVIDIA | Switch ハードウェア対応の背景、ブート連鎖、DT | 各プロジェクトに従う |
| [imbushuo/NintendoSwitchPkg](https://github.com/imbushuo/NintendoSwitchPkg)（`09c3ade4`）、[tianocore/edk2](https://github.com/tianocore/edk2) | TianoCore、imbushuo（Bingxing Wang）、ARM Limited、Linaro、Red Hat、Andrei Warkentin、Intel Corporation（`LaffStd.c` 由来の 8x19 フォント）、naehrwert（`Types.h`）、各ポート作者 | UEFI 側（`drivers/t214-aula/upstream-src/` のパッチ。本体は含まない） | BSD-2-Clause-Patent ほか（`drivers/t214-aula/NOTICE.md`） |
| [microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples)（`d49bb7d7`、`reference/ms-pep/`） | Microsoft | PEP の参照 | MS-PL |
| [microsoft/graphics-driver-samples](https://github.com/microsoft/graphics-driver-samples)（`de4a2161`） | Microsoft | WDDM の割り当て・ハンドル管理の参照（`TegraGpu/windows/allocation.c`） | MIT |
| [microsoft/Windows-Driver-Frameworks](https://github.com/microsoft/Windows-Driver-Frameworks)、[microsoft/Network-Adapter-Class-Extension](https://github.com/microsoft/Network-Adapter-Class-Extension) | Microsoft | KMDF の割り込み記述子、NetAdapterCx のアダプタ作成の参照（コードは含まない） | MIT |

### ビルド・実行時に取得するもの（本リポジトリには含めない）
| 名前 | 作者 | 用途 |
|---|---|---|
| [mstorsjo/llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | mstorsjo | OLED 上流の NVK ビルド用コンテナ |
| [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers)、[Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader) | Khronos Group | OLED 上流の NVK ビルド（ARM64 ローダー） |
| [PowerShell/Win32-OpenSSH](https://github.com/PowerShell/Win32-OpenSSH) | Microsoft / PowerShell チーム | OLED 上流の任意の SSH（ARM64 ビルドを別途取得） |
| Rust クレート（`tools/fwfetch/THIRD-PARTY.md`：adler2、crc32fast、flate2、sha2、serde、serde_json、memchr、miniz_oxide、libc ほか） | 各クレートの作者（[crates.io](https://crates.io)） | fwfetch の実行ファイル（MIT 側で使用） |
| Microsoft の WDK / SDK / WiFiCx、公式ドキュメント（Microsoft Learn） | Microsoft | ビルド・仕様の根拠（コードは含まない） |
| NVIDIA / Maxim / TI / Broadcom の公開データシート・公開ソース | 各社 | 各ドライバの README に個別記載 |

### 作業
- ソースの統合・整理、静的検査、CI、fwfetch、`build/`、OLED 枠の移動、スリープ・HDMI の設計、テスト署名キット：**Claude（Anthropic）による作業。steck0714 の指示と所有物に基づく**。
- 含めないもの：Broadcom ファームウェア、NVIDIA の GPU / XUSB ファームウェア、`bl31.bin`、秘密鍵（`tools/fwfetch/` が利用者の SD から取り出す）。

## ライセンス・出典

- winx 由来のソース: 各ディレクトリの `LICENSE`（`drivers/*/LICENSE`）に従う。出典は winx の `NOTICE.winx.md`（hayato41200、ruje0504-2、CTCaer 等）。
- `reference/windows-switch/`: DanilKhmyrov/windows-switch の `LICENSE`（GPL-2.0）をそのまま同梱。再配布条件に注意。
- 出典と権利者の一覧は `CREDITS.md`。
- Broadcom ファームウェア、NVIDIA の GPU / XUSB ファームウェア、`bl31.bin` は含めない（NVIDIA 側は Windows 向けの再配布が許諾外: `reference/l4t/NOTICE.md`）。利用者の実機から `tools/fwfetch/` で取り出す。ビルド済みの `.sys` / `.cat` / `.exe` は `build/` に置いてよい（2026-11-18 から。`COPYING.md`）。
- ライセンスの全体像: `COPYING.md`。秘密鍵(`.pfx` 等)は `.gitignore` で除外。
