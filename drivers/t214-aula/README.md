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

`kit/` には**テキストだけ**(INF・ps1・cmd・ini・`PUT_*_HERE.txt`)を無改変で置いた。`.sys` / `.exe` は `build/HEG-001/` に置く
(`python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip`。`manifest/` のサイズと SHA-256 に一致したものだけ書く)。
`bl33.bin` / ビットマップはここでは扱わない。

## GitHub Actions でのビルド(他の型番と同じ CI 方式)

`build/models.json` の HEG-001 は `build: ci`。`tools/ci/drivers-t214-aula.json` の一覧を `tools/ci/Build-AllDrivers.ps1 -Model HEG-001` が
(Lite と同じ `drivers/t214-hoag/tools/Build-Driver.ps1` で)ビルドし、1 つの証明書でテスト署名する。`swhid` / `swbatt` / `swagentsvc` はこのフォルダのソース、
`switchbt` / `switchaudio` / `switchwifi` は `drivers/t214-hoag/` の同系統ソース(`kit/` の INF と HWID・版が一致)。結果は `build/HEG-001/drivers/agent/`。
**CI は未実行・実機未確認。** 上流バイナリの照合に戻すにはワークフローを `heg001_upstream = true` で実行する。詳細は `build/HEG-001/README.md`。

## 上流のソース(`upstream-src/`)

windows-switch の `main`(ユーザー提供の `windows-switch-main.zip`)から、**バイナリと画像を除いた 110 ファイル**を置いた。
うち 7 ファイル(下の「ドライバ別フォルダ」)は `drivers/<ドライバ名>/` に移した。残りの 103 ファイルは**無改変**で `upstream-src/` にある:
`shim/`(EL2 shim)、`uefi/` と `patches/`(NintendoSwitchPkg への 27 パッチ)、`desktop/` のカード作成・設定スクリプト、
`drivers/` のビルドスクリプト・Dockerfile・`compat.c`・`test-cert/`・`nvk/`、`docs/`。

## ドライバ別フォルダ(`drivers/`、t210-icosa / t214-hoag と同じ並べ方)

| フォルダ | 中身 | 上流での位置 |
|---|---|---|
| `drivers/SwitchHid/` | swhid(タッチ・音量・電源ボタン・RTC・OLED 輝度) | `drivers/swhid/swhid.c` |
| `drivers/SwitchBattery/` | swbatt(電池・充電・ファン) | `drivers/swbatt/swbatt.c` |
| `drivers/SwitchTouchRead/` | touchread(swhid が `#include` するタッチ I2C 部) | `drivers/touchread/touchread.c` |
| `drivers/SwitchAgent/` | swagentsvc(起動エージェントのサービスホスト) | `desktop/swagentsvc.c` |
| `drivers/SwitchWifi/` `SwitchBluetooth/` `SwitchAudio/` | INF のみ(`.sys` は winx Lite 系の配布物) | `drivers/switch{wifi,bt,audio}/*.inf` |

- `.c` の 4 本には**作者・出典のクレジットブロック**(`W4T-CREDIT-BEGIN`〜`END`)を先頭に入れた。上流のファイルには作者・著作権の行が無いので、
  READMEが所有者と書いている DanilKhmyrov を「出典・所有者」として書き、個人の著作権行を新しく作ってはいない。INF は上流の表示を残すため無改変。
- 上流の `build*.sh` と `#include "../touchread/touchread.c"` は上流の配置を前提にしている。**`layout.json`** が移動先と上流の位置、上流の SHA-256 を持ち、
  `python tools/aula_layout.py --check` が「クレジットブロックと `#include` の 1 行を除くと上流とバイト一致」を確かめ、
  `python tools/aula_layout.py --materialize <空のディレクトリ>` が上流と同じ形の木を作る(ビルドはそこで行う。こちらではビルドしていない)。
**このリポジトリを作った環境ではビルドも実行もしていない**(WDK・Docker・実機が無い。CI は上の節)。v0.8.0 のバイナリとの一致も、スクリプト・INF・`swagentsvc.exe` の SHA-256 で
確かめただけで、`swhid.sys` / `swbatt.sys` がこのソースから出来る証拠は無い。出自・除外したもの・確認内容は `UPSTREAM-SOURCE.md`、**ファイルごとのライセンス監査は `LICENSE-AUDIT.md`、帰属表示は `NOTICE.md`**。

## 再配布できないファイルは実機から取る(`tools/fwfetch`)

```powershell
# 本体(Windows/PC どちらでも)。SD カードを挿して管理者ターミナルで(fwfetch.exe は build-fwfetch.yml が作る。手元なら cargo build --release):
fwfetch.exe --profile aula --auto --deploy E:\    # E: = windows-switch を展開したブート領域(自分の文字に読み替える。C: とは限らない)
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

## ファン・GPU 電源ブリッジ(swbatt)

- OLED のファンと `\Device\SwitchBacklight`(GPU 電源ブリッジ、IOCTL `0x222410`)は、Lite の SwitchFan ではなく `swbatt.sys`
  (`drivers/SwitchBattery`、`ROOT\SWBATT`)が担当する。CI の HEG-001 のドライバ一式(`tools/ci/drivers-t214-aula.json`)で必須としてビルドする。
- swbatt が実装しているブリッジのコードは `0x100`〜`0x108`(アドレス・PLLP・温度・EMC クロック・DVFS 方針・GPU 報告)。
  ruje0504-2/mesa-switch-woa `1dbc9e5`(`tools/ci/mesa-switch-woa.rev`)の ABI にある `0x109`〜`0x10f`(EMC 投票・レールの取得/解放など)は**未実装**で、
  `STATUS_INVALID_PARAMETER` を返す(安全側)。このリポジトリの TegraGpu が使うのは `0x100`〜`0x102` だけで、いまの組み合わせでは足りている。
  値の一致は `tools/check_swbatt_bridge.py` が CI で確認する(不一致なら失敗、未実装は一覧だけ)。
- `swbatt.c` は上流(windows-switch)とのバイト一致を検査しているので、ここでは書き換えていない。
- Lite の SwitchFan(v33)は Aula に入れない。

## 未確認・未対応

- OLED の GPU: `w64.cmd` に実験的な GPU 導入(`agent\gpu\`)があるが、v0.8.0 の zip には中身が無い。
  NVIDIA の GPU ファームは `--profile all` で集められるが、OLED で動く証拠は無い。
- OLED 輝度(swhid 経由)・長期動作・スリープ復帰・USB-C ホスト: 上流の報告以上のことは分からない。
- Aula の WinX 側(`acpi/`・SWOA 系ドライバ)の専用プロファイルは**無い**。
- `upstream-src/` は未ビルド。上流の `README.md` が参照するスクリーンショット(`docs/screenshots/` ほか)は、画像なので置いていない。

ライセンス: windows-switch 由来(GPL-2.0、`LICENSE` 同梱)。INF の元は hayato41200 / ruje0504-2 のドライバ(`CREDITS.md`)。
