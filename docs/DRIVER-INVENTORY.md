# Switch で動いている Windows ドライバ 全一覧

調査日 2026-11-16。対象は渡された 11 個の zip（SwitchDrivers 3 版 + manifest、windows-switch main 2 版、
windows-switch の loader/drivers v0.1.1・v0.8.0、hayatodriverfix 内の winx `usually` ツリー）。
**ビルドも実機試験もしていない。ソース・INF・DSDT・manifest・README の突き合わせだけ。**
INF の ID 検査は `tools/check_hwid_matrix.py` で再実行できる。

## 1. 系統（誰が何を持っているか）

| 系統 | ACPI 側の ID | 中身 | ソース |
|---|---|---|---|
| **winx / SWOA 系**（これが「WinX 互換」の基準） | `ACPI\SWOA0101`〜`0F01`、`NVDA0101` | ACPI リソース＋SpbCx/GpioClx で書かれた KMDF/WDM ドライバ群 | `drivers/t210-icosa`、`drivers/t214-hoag` |
| SwitchDrivers（ビルド済み kit） | 同上（SWOA 系） | winx t210 のビルド済み `.sys` + `.cat` + `.ps1` | zip のみ（`.sys` は同梱しない） |
| windows-switch（DanilKhmyrov 版） | `ACPI\SWTS0001`、`ROOT\SWBATT`（自前） | **ACPI リソースを使わず MMIO を直叩きする一体型** swhid / swbatt | `reference/windows-switch/` |
| windows-switch v0.8.0 の agent | `SWOA0C01` / `SWOA0B01` / `SWOA0D01` | winx Lite 系の audio 1.0.0.9 / bt 1.0.0.19 / wifi 0.5.7 をそのまま同梱 | `.inf` のみ参考保存 |

windows-switch は Mariko(OLED) で（ここでは `drivers/t214-aula/` に分離して置いた）、Wi-Fi・BT・音声だけ winx の Lite 系 SWOA ドライバを取り込み、
タッチ・電池・ファン等は自前の一体型で動かしている。両系統は同じ I2C/GPIO/PWM を取り合うため **同じカードに併用できない**
（`docs/WINDOWS-SWITCH-COMPAT.md`）。

## 2. ドライバ別（SWOA 系）

「版」は INF の DriverVer。t210 = Erista(Icosa)、t214 = Lite(Hoag)。`minUefi` は SwitchDrivers manifest の値。

| 機能 | サービス / sys | HWID | t210 版 | t214 版 | minUefi | 起動種別 | 備考 |
|---|---|---|---|---|---|---|---|
| GPIO | tegra210gpio | SWOA0101 | 1.0.0.4 | 1.0.0.4 | R367 | demand | GpioClx。音量キーは DSDT の `ACPI0011` 経由 |
| I2C | tegra210i2c | SWOA0201 | 1.0.0.4 | 1.0.0.5 | R370 | demand | SpbCx。t214 は I2C5 のゲート処理あり |
| 電池/USB-C | switchbattery | SWOA0301 (+0314 Lite) | 1.0.0.8 | 1.0.0.11 | R371 | demand | battc。BM92T/BQ24193 の USB-C ホスト電源（v4〜）。t210 1.0.0.7 は IRP 所有権修正を hayato 監査で取り込み。1.0.0.8(11/17)で Dx 停止/再開と I/O ターゲットの解放・二重オープン防止を追加（未ビルド） |
| ファン/バックライト | switchfan | SWOA0401 (+0402 Lite) | 1.0.0.3 | 1.0.0.13 | R372 | demand | `\Device\SwitchBacklight` を公開（GPU 電源ブリッジの入口） |
| タッチ | switchtouch | SWOA0501 / 0502(Lite) | 1.0.0.7 | 1.0.0.4 / Lite 1.0.1.2 | R373 | demand | FTS4/FTM4、I2C3 0x49。HID（VHF 系） |
| 電源ボタン | switchpwrbtn | SWOA0601 | 1.0.4.1 | 1.0.0.8 | R374 | demand | MAX77620 EN0 を 50ms ポーリング、VHF の System Power Down。Lite 1.0.0.8 は `\Device\SwitchCoreVoltage` で SwitchFan のコア電圧要求(VDD_CORE)も受ける |
| CPU クロック | switchcpu | SWOA0701 | 1.0.0.4 | 1.0.0.3 | R375 | demand | |
| RTC | switchrtc | SWOA0801 | 1.0.0.2 | 1.0.0.2 | R377 | demand | MAX77620 RTC |
| センサー | switchsensor | SWOA0901 | 1.0.0.2 | 1.0.0.1 | R379 | demand | 明るさ・傾き。Lite は SIO(`switchsio`, SWOA0902, 0.2.5.0) も |
| Bluetooth | switchbt | SWOA0B01 | 1.0.0.19 | 1.0.0.19 | R390 | demand | BCM4356 UART-D(H4)。t210 は 11/17 に t214 の 1.0.0.19 ソースへ更新（部分パッチ検証・BootPending・UART-D CAR 復元。単体テスト通過、未ビルド） |
| 音声 | switchaudio | SWOA0C01 | 1.0.0.7 | 1.0.0.9 | R402(R403でHP) | demand | WaveRT。ALC5639/I2S1。48kHz/16bit/stereo のみ |
| Wi-Fi | switchwlan(WiFiCx 0.6.13) / switchwifi | SWOA0D01 / 0D03(Erista) | 0.6.13 / 0.5.7 | 0.5.7 | R405 | demand | **Erista は実機未検証**。WDK 10.0.26100 必須 |
| USB Falcon | tegraxusb | SWOA0D02 | - | 0.1.0.0 | - | demand | 物理確認待ち。xHCI 本体は inbox `USBXHCI`（`PNP0D10`） |
| SD ホスト | nvtsdhc | NVDA0101 | 0.3.2.1 | 0.4.0.1 / WINARM 0.5.1.2 | - | **boot(0)** | 唯一のブートスタート。読めないと 0xc000000f の恐れ |
| GPU(診断) | tegragpuprobe / tegragpurender | SWOA0F01 | (GM20B-Diagnostic, S0–S2) | 0.1.0.60 / 0.1.0.66 | - | demand | 完成した WDDM ではない |
| 表示 | SampleDisplay(winx-display, BDD) | WINX0001 | 0.1.0.1 | 0.1.0.1 | - | demand | GOP フレームバッファ用 |
| 診断 | swoadiag / SwoaHello | (INF 無し) | - | - | - | - | USB 診断・段階0の読み込み試験 |

## 3. SwitchDrivers 各版の差

| ファイル | 2026.11.07 | 2026_11_2_5 | 2026.11.8.1 | winx `usually` ソース(t210) |
|---|---|---|---|---|
| audio | なし | 1.0.0.7 | 1.0.0.7 | 1.0.0.7 |
| battery | 1.0.0.6 | 1.0.0.6 | 1.0.0.6 | **1.0.0.7** |
| bluetooth | 1.0.0.13 | 1.0.0.13 | 1.0.0.13 | **1.0.0.15** |
| sd (nvtsdhc) | 0.3.2.0 | 0.3.2.0 | 0.3.2.0 | **0.3.2.1** |
| wlan | なし | なし | 0.6.13.0 | 0.6.13.0 |
| その他(gpio/i2c/fan/touch/pwrbtn/cpu/rtc/sensor) | 同一 | 同一 | 同一 | 同一 |

- manifest（`reference/switchdrivers-manifest-2026.11.07.json`）の `drivers.sha256` は `SwitchDrivers-2026.11.07.zip` の実ファイルと一致した。
- manifest の app/patcher（SwitchCenter 0.7.0 / SwitchPatcher 0.4.0）は添付されていない。
- 太字はソースがビルド済み kit より新しい。**再ビルド・再署名・再インストールが必要**。

## 4. windows-switch 側のドライバ

| 名前 | 役割 | winx の対応物 | 差 |
|---|---|---|---|
| swhid | FTS4 タッチ(HID 5 点) + 音量(Consumer Control) + 電源(短押し=画面オフ/4 秒=Ctrl+Alt+Del) + RTC オフセット復元 + OLED 輝度(DSI) | switchtouch + switchpwrbtn + switchrtc | **音量→Consumer Control、4 秒長押し Ctrl+Alt+Del、短押し画面オフ、RTC オフセット**は winx 側に同等なし(音量は DSDT `ACPI0011` 経由の別設計) |
| swbatt | MAX17050/BQ24193/TMP451 電池 + ファン曲線 + 過熱シャットダウン + ヘッドホン検出 + USB-C ポート電源 + GPU 電源ブリッジ | switchbattery + switchfan + switchaudio | 機能は概ね重複。GPU ブリッジは winx では switchfan の `\Device\SwitchBacklight`(IOCTL 0x222410) |
| touchread | タッチの診断用リーダ | - | 参考のみ |
| swagentsvc / tune.ps1 | 初回セットアップ・ガベージ削減(サービス/UWP/視覚効果) | SwitchCenter/Patcher(app) | 方向は近い |
| mesa-switch-woa(WDDM+NVK) | GM20B のユーザー/カーネル GPU | tegragpu*(Lite) | 本体ソースはこの zip 群に無い(サブセットのみ) |

## 5. 確認できた問題・要注意点

1. **Erista の Wi-Fi 系 INF が 3 つある**: `switchwlan`(SWOA0D01, 0.6.13)、`switchwprobe`(SWOA0D01, 0.4.0)、`switchwifi-erista`(SWOA0D03, 0.5.7)。
   前 2 つは同じ HWID。同時に入れると Windows が新しい方を選ぶだけで、意図しない側が掴む恐れがある。**どれか 1 つだけ入れる**。
2. **`nvtsdhc` だけブートスタート**。INF 版数・`testsigning`・`System32\drivers` への配置を最初に確認（hayato 監査の指摘）。
3. DSDT に INF の無い ID: `NVDA0100/0210/0211/0212/0213`（UART/XHCI/EHCI 系。inbox ドライバ想定）と `WINX0001`（表示）。
4. 検査上は t210 / t214 とも **すべての INF HWID が DSDT に存在**（フラグ付き ID は該当ビルドフラグ時のみ出る）。
5. 添付の windows-switch zip 2 つ（c559d5b1 と e890dc28）は版が違う（77 → 118 ファイル、README 17KB → 50KB）。後者を参照した。

> 2026-11-17: t210 の Fan / Battery / Sensor / CpuClock / PowerButton / Touch の版は、スリープ(Dx)時のポーリング停止/再開を足した版。詳細は `docs/SLEEP-AND-MISSING-DRIVERS-ja.md` の追補。
