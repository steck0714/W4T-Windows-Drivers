# UEFI / EDK II 側から見た不足点の点検(2026-11-20)

対象: このリポジトリ(`test` ブランチ)の Windows ドライバ一式を、UEFI(EDK II、winx の BL33)から起動して動かすとき、UEFI 側に足りないものがあるか。
**範囲は静的な点検だけ。** EDK II(ARM64)のビルド、winx 本体(`steck0714/winx`、このリポジトリには無い)、実機は見ていない。
再実行できる部分は `python3 -I uefi/tools/list_uefi_requirements.py`(下の 2 章)と `bash tools/run_host_tests.sh`。

## 1. このリポジトリにある UEFI 側のもの

| 項目 | 状態 |
|---|---|
| `uefi/edk2/GpuHandoff`(GPU をリセット+クランプのまま Windows に渡す) | ホストテスト・構文チェック・ドライバとの整合検査(`check_uefi_contract.py`)は通る。**EDK II でのビルドと実機は未確認** |
| `uefi/edk2/GpuHandoffApp`(UEFI Shell 用 `GpuHandoff.efi`) | 同上。FDF には入れない(SD にコピーして使う) |
| `uefi/patches/winx-gpu-handoff.patch` | winx の `usually` に当たることは、README では確認済みとされている(今回は winx が手元に無いので再確認していない)。winx 本体のビルドは未確認 |
| BL33 / bl31 / 各種ファーム | **無い**(winx のビルド成果物。bl31・XUSB・NVIDIA・Broadcom は再配布不可で、`tools/fwfetch` が自分の SD から取り出す) |

つまり UEFI 側の機械的な裏付けがあるのは **GPU の引き渡し 1 件だけ**。

## 2. ドライバが UEFI に求める「引き渡し」と、検査の有無

`list_uefi_requirements.py` の出力(ドライバのソース・DSDT のコメントと SwitchDrivers manifest `minUefi` を突き合わせたもの):

| ドライバ | manifest minUefi | ソースが挙げる最新 | UEFI がやっておくこと | 機械検査 |
|---|---|---|---|---|
| TegraGpu | - | - | GPU がリセット中かつクランプ済み(CAR `RESET_X` bit24、PMC `0x7000e6d4` bit0) | **あり**(`check_uefi_contract.py`) |
| Tegra210I2c | R370 | - | I2C1 / I2C5 のクロック・リセット ON | なし |
| SwitchBattery | R371 | **R378** | BM92T(I2C1 0x18)と XUSB を動いたままにする、充電設定(R363) | なし |
| SwitchFan | R372 | **R376** | ファン 5V EN(PA5)・PWM のクロックと配線、デューティ 76、LCD_BL_PWM(PV0)を PWM0 へ | なし |
| SwitchTouch | R373 | R373 | パネル電源(LDO6 2.9 V、リセット PJ7 high)、I2C3 クロック | なし |
| SwitchCpuClock | R375 | R365 | CPU を PLLX 1017.6 MHz・1025 mV で起動 | なし |
| SwitchSensor | R379 | R379 | SPI4 / I2C2 のクロック(止まっていると読みで固まる) | なし |
| SwitchBluetooth | R390 | **R391** | UART-D のピン・クロック・チップ電源(PH4) | なし |
| SwitchAudio | (manifest に無い) | R403 | APE クロック、ALC5639 の設定、I2S1 開始 | なし |
| SwitchWlan / WifiProbe | (manifest に無い) | R405 | PCIe リンク、エンドポイントの列挙(ID・メモリ有効・BAR0)、ACPI のリソース | なし |
| Tegra210Sdhc | - | - | SDMMC1 のクロック(カードが使っていた値のまま) | なし |
| SwitchRtc | R377 | - | なし(UEFI の RTC は仮想で、ドライバがオフセットを戻す) | - |

### 見つかったこと
1. **GPU 以外の引き渡しは、ドライバのコメントと manifest にしか書かれていない。** UEFI 側を作り替えたとき(winx の更新、別の BL33 への移植)に壊れても、ホストテストでは分からない。
2. **manifest の `minUefi` がソースより古い**: SwitchBattery(R371 だが R378 の挙動を前提)、SwitchFan(R372 だが R376)、SwitchBluetooth(R390 だが R391)。manifest は出荷済みバイナリ(2026.11.07)の記述なので、ソースが新しいのは通常の差だが、
   **新しい版を出すときに manifest の行を更新しないと、古い UEFI でも入れられてしまう。** Audio と Wi-Fi は manifest に行自体が無い。
3. 旧版の点検(`docs/driver-gap-audit-ja.md`、2026-11-10)が挙げた UEFI 側の未実装は、今回のソースでも変わっていない:
   - Joy-Con: メニューが読む `WinXJoyConInput` を**公開するドライバが無い**(hekate `bdk/input/joycon.c` が出典候補)。
   - 内蔵 Wi-Fi / Bluetooth: `WifiBtApp` は診断用。SNP・認証・DHCP・Bluetooth HID は無い(USB NIC の `SWITCH_NET` は別物)。
   - XUSB: `XusbInit` は実験アプリで FDF に入らない。完全な xHCI ホストではない。
   - 音声: `LiteAudio` は Hoag 向けの任意 DXE。他機種は未確認。
   - EfiFs は読み取り専用扱い。
4. GPU 引き渡し(`uefi/README-ja.md` の「入れていないもの」)で未実装: 準備ができたときだけ ACPI の GPU0 を見せる処理、GPU レールの UEFI 側制御。
   OLED の呼び出し位置は winx の `SwitchBootMenu.c` 前提で、DanilKhmyrov 版の構成では位置が違う可能性がある。
5. HDMI(ドック)は設計だけ。`docs/HDMI-DESIGN-ja.md` の経路 A(UEFI で GOP をドック側へ)は未実装、H2b 以降(EDID・DPAUX・リンク学習・SOR/DC)も未着手(`PROGRESS.md`)。
6. スリープ(S0 低電力 / PEP / SC7)は未実装。UEFI 側ではなく ATF(bl31)の SC7 と `sc7entry.bin` / `sc7exit.bin`(再配布不可、fwfetch 対象)が前提。
7. Iowa(HAC-001-01)には UEFI を含めドライバ一式が無い。Aula(OLED)は windows-switch 系で、winx の SWOA 系 UEFI とは別構成(併用不可、`docs/WINDOWS-SWITCH-COMPAT.md`)。

### 次にやると効くこと(小さい順)
- `uefi/tools/check_uefi_contract.py` と同じ形で、**ファン / タッチ / センサー / BT / I2C の引き渡し**(レジスタとビット)をドライバのソースから表にして検査する。ドライバ側の値を読み、UEFI 側のコードがある場合は突き合わせる。
  winx のソースが手元に無いので、まずは「ドライバが前提にしている値の一覧」を固定するところまで。
- manifest に Audio / Wi-Fi の行を足し、`minUefi` を R378 / R376 / R391 / R403 / R405 に合わせる(出荷する版を決めてから)。
- winx(`usually`)を clone して `build -p ... -a AARCH64` を CI に入れ、`GpuHandoff.efi` と FV の容量を記録する(この点検では EDK II のビルドは行っていない)。

## 2b. 機種ごとに UEFI が違う点(忘れないこと)

UEFI(winx の BL33)は**機種ごとに別のビルド**で、ボードのフラグ・GPU の電源バックエンド・引き渡しが違う。1 つの表を全機種に流用しない。

| 機種 | ドライバ側 | UEFI 側(このリポジトリで分かる範囲) | 注意 |
|---|---|---|---|
| Erista(HAC-001) | `t210-icosa` | `SWITCH_BOARD_ERISTA` + `SWITCH_ERISTA_GPU`。GPU は `t210_power.c`(Erista の HIDREV) | manifest の `minUefi` はこの系統の表。Wi-Fi はコード接続のみ・実機未検証。XUSB は Erista 用ファーム(`tegra21x_xusb_firmware`) |
| Lite(HDH-001) | `t214-hoag` | `SWITCH_LITE_GPU`(既存フラグ)。`LiteHomeBoot.c` の `BootWindowsFromMenu()` から引き渡し。GPU は `t214_power.c` | タッチは Hoag 専用の FTM4 設定。`LiteAudio` も Hoag 向け。XUSB は Mariko 用ファーム(`tegra210b01_xusb_firmware`)。manifest に Lite の行は無い |
| OLED(HEG-001) | `t214-aula`(windows-switch 系) | `SWITCH_BOARD_OLED` + `SWITCH_OLED_GPU`。呼び出しは `SwitchBootMenu.c` 前提で、DanilKhmyrov 版では位置が違う可能性 | SWOA 系ドライバとは併用不可。専用の UEFI プロファイル・パネル/電源/GPU の検証は未 |
| Iowa(HAC-001-01) | 無し | プラットフォーム識別・一部 ACPI のみ | Hoag / Erista の設定を流用する根拠が無い(基板・電源・I2C・SD 配線) |

`list_uefi_requirements.py` も、t210 と t214 を別の表で出すようにした(OLED のドライバは別系統で、この表の対象外)。manifest 比較は t210 だけ。

## 2c. 実ソースとの照合(2026-11-20、winx `usually` afb24cf / windows-switch main ebff45d)

2 章は「ドライバ側の記述」だけだったが、winx と windows-switch を取得して、UEFI 側の実コードと突き合わせた。再実行: `python3 -I uefi/tools/list_uefi_requirements.py --winx <winx の clone>`(DSC に各機種が頼るフラグ・モジュールがあるかを調べる。ビルドや実機動作の証明ではない)。

### 分かったこと
1. **UEFI は機種ごとに別物で、揃い方が違う。**
   - Erista: R363〜R379 の起動処理(USB-C PD/充電、CPU の PLLX 1017.6 MHz、ファン、バックライト、タッチ電源、SPI4/I2C2 クロック)は 1 つの `UsbPowerAudit.c`(6192 行、`SWITCH_WOA` でビルド)に入っている。ドライバのコメントの `UEFI R3xx` はこれを指す。
   - Lite(Hoag): 上の処理は**使われない**(`LiteAudio` のコメントに「Erista の USB/CPU 起動処理を通さない」とある)。代わりに機能ごとのフラグと小さなモジュール: `SWITCH_LITE_FAN`(LiteFanPower)、`_BLUETOOTH`、`_AUDIO`、`_XUSB`(LiteUsbPrepare)、`_PERFORMANCE`(LitePerformanceDxe)、`_RESET`、`SWITCH_WIFI_RUNTIME`、`SWITCH_LITE_GPU` など。**フラグを付けずにビルドすると、対応する Windows ドライバの前提(クロック・電源)が満たされない。**
   - OLED(Aula): winx の中には `SWITCH_BOARD_OLED` のプロファイルと `SWITCH_OLED_GPU` の引き渡ししか無い。**OLED で実績のある UEFI は winx ではなく、windows-switch の別系統**(imbushuo `NintendoSwitchPkg` に当てる `patches/*.patch` の列。`touch` / `fan` / `wifi` / `bluetooth` / `audio` / `usb` / `gpu` / `gpudisplay`、Docker の `uefi/build.sh`)。
   - Iowa: `SWITCH_BOARD_MARIKO` のプロファイル(識別用ヘッダ)だけ。起動処理は無い。
2. 上の対応が**この表どおり DSC にあることは確認できた**(`--winx` が全項目 ok)。ただし、W4T 側にはドライバ一式とフラグの組み合わせを突き合わせる検査が無かったので、今回の `--winx` がその最初の形。
3. `GpuHandoff.c/.h` は winx `usually` と W4T の `uefi/edk2/GpuHandoff/` が**一致**(diff 空)。winx 側に `Drivers/GpuHandoffApp/` も入っていて、パッチは取り込み済み。
4. 型番ごとの UEFI が揃わない帰結:
   - **Iowa と OLED(winx 経由)は、SWOA 系ドライバが前提にする UEFI の起動処理が無い**。W4T に Iowa のドライバ一式が無い理由と整合する。OLED は windows-switch 系で、ドライバ(`t214-aula`)もその UEFI とセット。
   - ruje0504-2/winx(`hdh001-hekate-sd`、2c993ef)は、`Platforms/Switch` の機種別構成と `GpuHandoffApp` が無い古い系統(steck0714 の `usually` との差は 208 ファイル)。Lite の起動に使う UEFI はどの枝か決めて、フラグ一式を固定するのがよい。
   - steck0714/windows-for-Switch は NintendoSwitchPkg の流れを汲む Erista 向けの親(`UEFI R3xx` の出所)で、ドライバ側のコメントの版番号はここに対応する。

### 次にやると効くこと(機種別)
- Lite: 使うドライバごとに必要な `SWITCH_LITE_*` フラグの組を 1 つ決め、`build/HDH-001/` に「この UEFI フラグでビルドした BL33 と組で使う」と書く。
- Erista: `SWITCH_WOA` + `SWITCH_WIFI_ERISTA` + `SWITCH_ERISTA_GPU` の組を同様に固定。
- OLED: windows-switch の UEFI パッチ列と `t214-aula` のドライバの版を対応づける(winx の OLED フラグと混ぜない)。

## 3. build/ への自動配置(今回の変更)

「ドライバは全部ビルドできるのに、`build/<型番>/` に `.sys` などが入らない」件。原因は、CI のコミットが**手動実行で `commit_build` を ON にした時だけ**動く作りだったこと。

- `.github/workflows/build-all-drivers.yml`: `commit-build` を、`main` / `master` / `test` への push のあと**自動で**動かす。手動実行の `commit_build` は既定 ON。
  成功した型番だけを置く(失敗した型番のフォルダは前のまま)。push の自動コミットにはアプリ(大きい)を含めず、手動実行だけで置く。ブランチの最新を取得してから push し、競合時は rebase して再試行する。
- `tools/ci/Build-AllDrivers.ps1`: 手元の Windows で `-Model <型番>` を付けて実行すると、終わりに自動で `build/<型番>/` へ置く(`-NoAssemble` で止められる。CI は自分の手順で置くので止めている)。
- `tools/build/sync_build.py`(新規): 自分の clone へ、成功した最新の実行のアーティファクトを取り込む(`gh` が要る)。`git pull` で足りないとき・コミットさせたくないとき用。秘密鍵は拒否し、`check_no_proprietary.py` を最後に走らせる。テストは `tools/build/tests/test_build_tools.py`(偽の `gh` を使う 6 件)。
- 注意: 署名用のシークレット(`SWOA_PFX_BASE64` / `SWOA_PFX_PASSWORD`)が無いと、実行ごとに別の自己署名になり、毎回 `.sys` と `.cer` が変わってコミットが増える。固定したいならシークレットを入れる。
- **この環境では GitHub Actions も PowerShell も動かせないので、ワークフローと `.ps1` の変更は実行していない**(YAML の構文と PowerShell の括弧の対応だけ確認)。`sync_build.py` と `list_uefi_requirements.py` はホストでテスト済み。
  最初の push のあと、Actions の `commit-build` のログと `build/` のコミットを確認してほしい。
