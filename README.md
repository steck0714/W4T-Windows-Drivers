# W4T-Windows-drivers

(Tegra X1) 上の Windows ARM64 で動いているドライバを集め、**WinX の ACPI(SWOA 系 ID)に合わせて**
まとめたリポジトリ。Erista(T210/Icosa)、Lite(T214/Hoag)、**OLED(T214/Aula)を別枠**で扱う(Iowa は専用の枠なし)。

> **未ビルド・未実機。** この環境には WDK が無く、`.sys` は作っていない。確認したのはソース/INF/DSDT の突き合わせと、
> ホストで動く単体テストだけ。実機で動く証明ではない。

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

## 2026-11-18 追加分（4 回目: スリープ→HDMI）
- **スリープ**: `acpi/t210/lpi.asl.inc`（CPU `_LPI`、`SWITCH_LPI` で opt-in。コア停止は Switchroot DT が c7 を無効にしている根拠で無効フラグ付き）、`SwitchSleep/`（PSCI 状態 ID と SC7 前提検査。ATF `ed2ef6be` と突き合わせ）。`tools/check_dsdt_compile.py` が iasl で 3 構成をコンパイル。→ `docs/SLEEP-DESIGN-ja.md`
- **HDMI**: `SwitchDisplay/src/dp_link.h`（L4T `tegra_dc_dp_calc_config` の移植。原本との差分テスト 2160 通り一致）。→ `docs/HDMI-DESIGN-ja.md`
- 再開用の進捗は `PROGRESS.md`。**どれも WDK ビルド・Windows・実機は未確認。**

## 2026-11-18 追加分（3 回目）
- **ビルド済みバイナリを置いてよい方針に変更**（作者の許可）。置く場所は `build/<型番>/`。再配布できないファームウェアと秘密鍵は引き続き不可。`.gitignore`・`COPYING.md`・`tools/check_no_proprietary.py` を合わせた。
- **`build/` を型番別に新設**: `HAC-001`（Erista）は CI でビルドして置く設定まで。`HAC-001-01`（Iowa）はドライバ一式が無いので空。`HDH-001`（Lite）は CI 設定が未作成。`HEG-001`（OLED）は上流のビルド済みをハッシュ照合して置く。**このリポジトリを作った環境には WDK も Windows も無く、`.sys` は 1 つも作れていない。**
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

## ライセンス・出典

- winx 由来のソース: 各ディレクトリの `LICENSE`（`drivers/*/LICENSE`）に従う。出典は winx の `NOTICE.winx.md`（hayato41200、ruje0504-2、CTCaer 等）。
- `reference/windows-switch/`: DanilKhmyrov/windows-switch の `LICENSE`（GPL-2.0）をそのまま同梱。再配布条件に注意。
- 出典と権利者の一覧は `CREDITS.md`。
- Broadcom ファームウェア、NVIDIA の GPU / XUSB ファームウェア、`bl31.bin` は含めない（NVIDIA 側は Windows 向けの再配布が許諾外: `reference/l4t/NOTICE.md`）。利用者の実機から `tools/fwfetch/` で取り出す。ビルド済みの `.sys` / `.cat` / `.exe` は `build/` に置いてよい（2026-11-18 から。`COPYING.md`）。
- ライセンスの全体像: `COPYING.md`。秘密鍵(`.pfx` 等)は `.gitignore` で除外。
