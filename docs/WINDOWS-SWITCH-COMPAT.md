# windows-switch ドライバと WinX(SWOA) の互換性

## 結論

- windows-switch の **swhid / swbatt は WinX に載せられない**（載せるべきでもない）。両者は ACPI リソースを使わず、
  `0x7000C000`(I2C1) `0x7000D000`(I2C5) `0x6000D000`(GPIO) `0x7000A000`(PWM) などを `MmMapIoSpace` で直接叩く一体型で、
  WinX の `tegra210i2c`(SpbCx)/`tegra210gpio`(GpioClx) が同じレジスタを管理する。**併用すると同じバスを 2 つが操作する。**
- 逆に windows-switch の audio / bt / wifi は WinX Lite 系の SWOA ドライバそのもの（audio 1.0.0.9 / bt 1.0.0.19 / wifi 0.5.7）。
  HWID は WinX の DSDT と同じなので、**WinX 側でそのまま使える**。
- WinX にあって windows-switch にある機能のうち、WinX に無かったのは主に次の 4 つ。

| swhid/swbatt の機能 | WinX 側の現状 | 対応 |
|---|---|---|
| 電源ボタン 4 秒長押しで Ctrl+Alt+Del | なし | **移植済み**（t210 SwitchPowerButton 1.0.0.2、`HoldSasMs`） |
| 電源ボタン短押しで画面オフ | Lite の switchpwrbtn は「画面オフ中は Wake」を持つが、Erista は電源動作を Windows に委ねる | 未対応（電源プランの「電源ボタンを押したとき: ディスプレイをオフ」で代替できる） |
| 音量キーを Consumer Control で報告 | DSDT `BTNS`(ACPI0011, 標準ボタン) の設計。Windows 標準ドライバ側 | 未実機確認。ドライバ不要の設計 |
| RTC オフセットを覚えて起動時に時刻復元 | `switchrtc` が MAX77620 RTC を読む | 未移植（swhid の「RTC は書かず、NTP 後のオフセットだけ保存」方式のほうが Horizon を壊さない。要検討） |
| OLED 輝度を DSI(DC_BACKLIGHT)経由で制御 | winx-display / SwitchFan(`\Device\SwitchBacklight`) | 未移植。Aula(OLED) は専用プロファイル未整備 |
| USB-C ポート給電(BM92T/BQ24193) | `switchbattery` v4〜（Lite は 1.0.0.11） | WinX に同等あり |
| GPU 電源ブリッジ(MAX77812 M1) | `switchfan` が `\Device\SwitchBacklight`、IOCTL `0x222410` を公開（Lite） | Erista(MAX77621)向けは**書いていない**。電圧を書く経路を未検証で作らない |

## 併用してはいけない組み合わせ

| 組み合わせ | 理由 |
|---|---|
| swhid(`SWTS0001`) + switchtouch(`SWOA0501`) | 同じ FTS4(I2C3 0x49) |
| swbatt(`ROOT\SWBATT`) + switchbattery(`SWOA0301`) / switchfan | 同じ I2C1 / PWM |
| switchwlan と switchwprobe（どちらも `SWOA0D01`） | 同じ HWID に 2 つの INF |
| 古い SwitchDrivers kit と winx ソース版の同サービス | 版が違うと上書きされる（SwitchDrivers 2026.11.8.1 は battery 1.0.0.6 / bt 1.0.0.13 / sd 0.3.2.0） |

## 起動チェック（hayato 監査より）

`nvtsdhc.sys` だけがブートスタート。0xc000000f が出たら、回復環境で
`dir C:\Windows\System32\drivers\nvtsdhc.sys`、`bcdedit /enum all` の `testsigning`、INF の DriverVer を確認する。
