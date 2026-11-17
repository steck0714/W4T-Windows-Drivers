# hayato41200 のErista用ドライバと ruje0504-2 のLite用ドライバの突き合わせ（2026-11-15）

対象: `WindowsDrivers/t210/Icosa/drivers`（Erista、hayato41200 由来）と、ruje0504-2/Windows-for-Switch-Drivers `hdh001-winarm-bounded-write`（Lite、99ab6c8まで確認）。
**WDK が無い環境での作業なので、ドライバのビルドも実機テストもしていない。** 確認できたのはソースの突き合わせと、upstream 付属の静的テストだけ。

## 突き合わせの結果
1. **Erista側のドライバは、ruje側の過去コミットと完全に一致していた**（差分0行）。つまりErista版は「ruje側の古い版」で、ruje側にはその後の修正がある。

| ドライバ | Erista版 = ruje側のこのコミット | 以後のruje側の変更 |
|---|---|---|
| Tegra210Sdhc | c7a40e6 (10/29) | T214(Lite)専用が大半。汎用の修正は 7256814 の1件 |
| SwitchBattery | 7cb0353 (10/31) | IRPの所有権修正 a42882d、他はLite専用（USB-C host、充電終了） |
| SwitchBluetooth | 9886c58 (10/31) | キャンセル/所有権 34b5800、割り込み切断前の停止 ac1fc3a、他はLite専用 |
| SwitchPowerButton / SwitchSensor / SwitchAudio / SwitchFan | 各古いコミット | Lite専用（S0復帰、UARTゲームパッド、バックライト、冷却曲線） |
| SwitchTouch / Tegra210Gpio / SwitchRtc / SwitchCpuClock / Swoa* | 同一 | なし |

2. `Tegra210I2c` の差(26行)は、Lite専用の I2C5 ゲート処理で、Eristaのバグではない。
3. INFのACPI ID と DSDT の `_HID/_CID` を機械的に突き合わせた。Erista側の主要デバイス（GPIO、I2C、バッテリー、タッチ、ファン、RTC、電源ボタン、センサー、オーディオ、CPUクロック、Bluetooth、SD）で、**どのINFにも対応するDSDTデバイスがある**。ID不一致は見つからなかった。Wi-Fi（SWOA0D01/0D03）とGPU（SWOA0F01）は、ビルドフラグやUEFIアプリが実行時に出す設計なので静的DSDTに無いのは正常。
4. ruje側の `Dsdt.asl` とWinX側の差は、モデル別のフラグ（Lite GPU、Lite電源IRQ、Wi-Fi）と、SDホストの `_CCA`（WinXは `0` に固定、rujeは nvtsdhc のとき `0`、sdbus のとき `1`）だけ。nvtsdhc 構成ではどちらも `_CCA = 0` で同じ。

## ruje側の修正のうち、Erista版に取り込んだもの（未ビルド）
| 対象 | 取り込んだ修正 | 内容 | 検証 |
|---|---|---|---|
| SwitchBattery 1.0.0.6→1.0.0.7 | a42882d を手でマージ | battc にIRPを渡す前に KMDF のリクエストを作ってしまい、電源断のときキューが止まる問題の修正（`ClassLock` と WDM IRP preprocess） | upstream付属の `tests/test-irp-ownership.py` が通る |
| SwitchBluetooth 1.0.0.15 | ac1fc3a → 34b5800 を順に適用 | UART割り込み切断前にHCI作業とリトライを止める／キャンセル時に送信中フレームの所有権を失わない | 付属の `tests/test-cancel.py` が通る |
| Tegra210Sdhc 0.3.2.0→0.3.2.1 | 7256814 | バス電源OFF要求が実際にはレジスタへ書かれていなかった問題と、「既定シグナリング」の再要求でエラーを返す問題の修正 | 未実行（静的テストなし） |

INFの版数を上げたので、再ビルド・再署名したパッケージを入れ直す必要がある。ビルド済みの `.sys` はzipに含まれない。

## 取り込まなかったもの（理由つき）
- Lite専用: 冷却曲線、CPU 1497.6MHz、Lite用USB-C host、UARTゲームパッド、S0復帰、DDR200/SDR104。EristaのHWで意味が違うか、検証ができない。
- ruje側の新しい `SwitchBattery` は `PdBoard`（Erista/Lite）の切り替えを持ち、Erista動作を内包している。丸ごと置き換える手もあるが、差が大きいため今回は最小の修正だけにした。

## 症状との関係（仮説）
- **0xc000000f**: ruje側のドキュメントに、**ブートスタート（StartType=0）のドライバが読み込めないと 0xc000000f で起動できない**という実例がある（Lite用の実験的PEPが原因だった）。Erista側でブートスタートなのは `nvtsdhc.sys`（`System Bus Extender`）**だけ**で、他は全部デマンドスタート。したがって `nvtsdhc.sys` が `C:\Windows\System32\drivers\` に正しく入っているか、テスト署名とテストモード（`testsigning on`）が有効か、入れたバージョンが読めるか、が最初の確認点になる。
- **キーボードでBugCheck**: Erista側のどのドライバにも、USBホストを扱うコードはない（EHCIは標準ドライバ）。今回のドライバ修正は直接の原因ではない。`EHC0` の `_CCA` の件は別資料を参照。
- 今回のドライバ修正はbl33には含まれない。bl33は前回の2つのままでよい。

## 確認してほしいこと
1. 回復環境で `dir C:\Windows\System32\drivers\nvtsdhc.sys` と、`bcdedit /enum all` の `testsigning` の値。
2. Windowsに入れている `nvtsdhc` の版数（INFの DriverVer）。
3. BugCheckの名前とパラメータ。
