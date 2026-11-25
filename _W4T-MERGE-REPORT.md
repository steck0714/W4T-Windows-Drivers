# W4T-Windows-Drivers (test) 上流反映 v2 — 2026-11-25
**前回のzip(upstream-latest-20261125.zip)は使わないでください。** W4T側で直した修正を、上流の古い版で上書きしていました。
今回は「旧スナップショット(3d32f29)/W4T現状/上流最新」の三者比較で作り直しています。

zipの中身 = 変更・追加ファイルだけ(リポジトリと同じパス)。削除は `_REMOVED-FILES.txt`、残したW4T独自修正は `_KEPT-LOCAL-FIXES.txt`。

## A. W4T独自の修正(上流は未変更 → 残した)
上流が一度も触っていないので、上書きすると修正が消える。以下32ファイルを残した。
| 内容 | 対象 | 良い点 | 注意 |
|---|---|---|---|
| スリープ/復帰(SelfManagedIoSuspend/Restart)追加 | SwitchFan・CpuClock・Sensor・Touch・Battery | スリープ前にポーリング停止。Fanは76固定、CPUは既定クロックに戻してから寝る(過熱・過クロック放置を防ぐ) | 実機でスリープ→復帰を一通り確認したいところ |
| タイマー再武装の競合対策(Stopping再確認) | 上と同じ+Battery | 停止中にタイマーが再度走る隙を塞ぐ | — |
| Battery: ハンドル二重オープン(リーク)防止、ClassLock | SwitchBattery | 無効化→有効化の繰り返しでリークしない | — |
| Bluetooth 1.0.0.14〜19 | SwitchBluetooth(+397/-100) | パッチ全体の検証、部分ダウンロードで無線を公開しない、UART-DのCAR復元など安全側の修正が多い | 変更量が大きい。上流と大きく分岐している |
| I2c: 50us超の待ちを KeDelayExecutionThread に | Tegra210I2c | DISPATCH_LEVEL未満でCPUを占有しない。単位計算(−Us×10)も正しい | — |
| SDHC: 電源OFF後に確認、1.8V切替の重複防止 | Tegra210Sdhc | 電源が落ちたか確認して失敗を返す | — |
| INFのDriverVer更新 | 上記の各INF | 版が上がるのでPnPが確実に入れ替える | — |
| kitsのログ先を C:\ 固定から %SystemDrive% に | Switch-*/ *.ps1 | Windowsが C: 以外でも動く | — |
| tools/Build-Driver.ps1 | t210-icosa/tools | NoWarnAsError・CertThumbprint・ForceInclude・SDK自動検出・C++のclang対応。CIが使う新しい引数がある | 上流の版に置き換えるとCIが壊れる |
**懸念**: 独自修正が増えるほど上流の次の更新と衝突しやすい。上流へPRするか、修正一覧を残しておくと楽。

## B. 取り込んだ上流の更新
| 対象 | 内容 | 良い点 | 注意 |
|---|---|---|---|
| t210 SwitchGpu / SwitchDisplay | blit・gpcpll・idle・flip・present など追加。AutoTest系kit追加 | GPU/表示の段階テストが自動化される | 実験段階。実機未確認。古いテスト用.cmd 6本は新Gpu.ps1にモードが無いので削除 |
| t210 kits Install/Status.cmd | UAC昇格を自動化 | ダブルクリックで動く | — |
| t214 I2c 1.0.0.7 / Rtc .4 / Battery .14 / Audio .10 / Gpio .5 / Wifi 0.5.7.1 | 上流の方がDriverVerが新しいものだけ更新 | 新機能(Batteryの充電制限・テレメトリ等)と修正。必要なヘッダ・testsも一緒に追加 | t214側はW4T独自修正の有無を判別できなかった(比較元が無い)。git diffで確認を |
| reference/・build/ のスナップショット | hayato 293bba2/17804f0、mesa-switch-woa 68c4dd3 | 上流が非公開になっても参照できる | README・MANIFEST・CREDITSの固定値も更新済み |

## C. 取り込まなかった/手動で判断
1. **SwitchPowerButton(t210)**: W4Tは1.0.0.3系(HoldSasMs等)、上流は1.0.4.0(FastStartup対応=シャットダウンで電源が切れない問題の修正)。5か所が衝突し自動で混ぜられない。`SwitchPowerButton-upstream-1.0.4.0.patch` に上流の差分を出した。kitの Reset.ps1 / FastStartup-*.cmd もドライバ側が入るまで入れていない。
2. **t214 SwitchLiteTouch(1.0.1.2)・SwitchSensor INF(0.2.5.0)**: W4Tの方が上流(1.0.1.0 / 0.2.3.0)より新しいので据え置き。
3. **t214 kits・tools/Build-Driver.ps1・docs/hdh001**: 既存ファイルはW4T版を残し、新規ファイルだけ追加。上流とW4Tで差があるもの(Audio.ps1の版確認、Stage3-I2c.ps1のバス検出など)は手動マージ候補。
4. バイナリ3点(アイコン、SwitchWOA-Test.cer、mesaの.bin)は入れていない。

## 未確認
ビルド(WDK/ARM64)と実機は未実施。静的な比較とマージのみ。
