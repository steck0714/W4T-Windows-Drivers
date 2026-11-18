# t214-aula — Switch 有機EL(HEG-001 / Aula)で動作報告のある構成

`drivers/t214-hoag/`(Lite, HDH-001)とは**別の枠**。OLED で実機確認の報告があるものだけをここに置く。
Lite 側の設定・ドライバは OLED の根拠にならないので、ここへは流用していない。

## 根拠(何が「OLED で動いている」と言えるのか)

- DanilKhmyrov/windows-switch の**リリースノートが「実機確認は Mariko の Switch 有機EL(HEG-001) 1 台のみ」**と書いている
  (v0.7.x のノートは GitHub の releases ページで確認。v0.8.0 の同文は `docs/audit-2026-11-17-static.md` が引用)。
- つまり証拠は**配布者 1 人・1 台の報告**。こちらでは実機も WDK も無く、動作を再確認していない。
- 同梱ファイルの SHA-256 は `manifest/windows-switch-0.8.0.json`(アップロードされた 2 つの zip から計算)。

## 構成(windows-switch v0.8.0 = OLED 報告あり)

| 機能 | ファイル | 版・系統 | メモ |
|---|---|---|---|
| タッチ / 音量 / 電源 / RTC / OLED 輝度 | `agent/arm64/swhid.sys` (`ACPI\SWTS0001`) | windows-switch 独自 | ACPI を使わず I2C/GPIO を直接叩く一体型 |
| バッテリ / ファン | `agent/arm64/swbatt.sys` (`ROOT\SWBATT`) | windows-switch 独自 | 同上(I2C1 / PWM) |
| Wi-Fi | `agent/wifi/switchwifi.sys` | 0.5.7.0(winx Lite 系) | 要 BCM4356 ファームウェア |
| Bluetooth | `agent/bt/switchbt.sys` | 1.0.0.19(winx Lite 系) | 要 BCM4356A3.hcd |
| 音声 | `agent/audio/switchaudio.sys` | 1.0.0.9(winx Lite 系) | |
| 起動エージェント | `agent/swagentsvc.exe` + `w64.cmd` | | 起動のたびに設定・ドライバ導入を実行 |
| ローダー | `wp11/bl33.bin`, `wp64/bl33.bin` + `bootloader/ini/*.ini` | UEFI | **`bl31.bin` と XUSB ファームは別途**(下記) |

`kit/` には**テキストだけ**(INF・ps1・cmd・ini)を無改変で置いた。`.sys` / `.exe` は `build/HEG-001/` に置く
(`python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip`。`manifest/` のサイズと SHA-256 に一致したものだけ書く)。
`bl33.bin` / ビットマップはここでは扱わない。

## 再配布できないファイルは実機から取る(`tools/fwfetch`)

```powershell
# 本体(Windows/PC どちらでも)。SD カードを挿して管理者ターミナルで(fwfetch.exe は build-fwfetch.yml が作る。手元なら cargo build --release):
fwfetch.exe --profile aula --auto --deploy E:\    # E: = windows-switch を展開したブート領域
```

`bl31.bin` / XUSB ファーム / BCM4356 の Wi-Fi・BT ファームを、Switchroot が入った**自分の SD** から探して照合し、
`wp11\` `wp64\` `agent\wifi\` `agent\bt\` へ配置する。仕組み・限界は `tools/fwfetch/README-ja.md`。
Windows 側から SD の ext4 が見えない場合(Lite の SD ミニポートは WINARM/FAT32 のみ公開する設計。OLED で同じかは未確認)は、PC にカードを挿して実行する。

## Lite との分離ルール

- `drivers/t214-hoag/` の設定(FTM4 タッチ、SwitchFan の Lite 用 SWOA0402 など)を Aula に使わない
  (`docs/driver-gap-audit-ja.md`: 基板・電源・パネル・I2C・SD 配線の一致は未証明)。
- windows-switch(`SWTS0001` / `ROOT\SWBATT`)と winx(`SWOA*`)のドライバは**併用しない**
  (同じ I2C3 FTS4 / I2C1 / PWM を 2 つが操作する)。`docs/WINDOWS-SWITCH-COMPAT.md`。
- Wi-Fi / Bluetooth / 音声は winx Lite 系と**同じコード系統**だが、OLED で動いた根拠は windows-switch の配布物 1 件のみ。

## 未確認・未対応

- OLED の GPU: `w64.cmd` に実験的な GPU 導入(`agent\gpu\`)があるが、v0.8.0 の zip には中身が無い。
  NVIDIA の GPU ファームは `-Profile all` で集められるが、OLED で動く証拠は無い。
- OLED 輝度(swhid 経由)・長期動作・スリープ復帰・USB-C ホスト: 上流の報告以上のことは分からない。
- Aula の WinX 側(`acpi/`・SWOA 系ドライバ)の専用プロファイルは**無い**。

ライセンス: windows-switch 由来(GPL-2.0、`LICENSE` 同梱)。INF の元は hayato41200 / ruje0504-2 のドライバ(`CREDITS.md`)。
