# 上流最新との比較 (2026-11-25) と取り込み zip

W4T testブランチ(b663b03)を clone し、各上流の最新と比較した結果。**zip の中身 = 変更したファイルだけ(リポジトリと同じパス)**。削除は `_REMOVED-FILES.txt`。

## 比較結果(型番別)
| 型番 | 上流 | 上流の最新 | W4Tの状態 | 結論 |
|---|---|---|---|---|
| Erista (t210) | hayato41200/Windows-for-Switch-Drivers | `17804f0` (2026-11-24) | 取り込み済みの `17804f0` と同じ。INFのDriverVerも上流と同じかW4Tの方が新しい(Bluetooth .19、CpuClock .4、Sensor .2、I2c .5、Sdhc .3.2.1 など) | **SwitchPowerButton だけ上流が新しい**(1.0.4.0 > 1.0.0.3)。→ 本zipで取り込み |
| Lite (t214-hoag) | ruje0504-2/Windows-for-Switch-Drivers | `c5ae935` (2026-11-18) | W4Tは `c5ae935` と一致(差はW4Tのクレジット欄9行のみ)。LiteTouch 1.0.1.2 / Sensor INF 0.2.5.0 はW4Tの方が新しい | **更新なし** |
| OLED (t214-aula) | DanilKhmyrov/windows-switch | `ebff45d` (2026-11-15) | `upstream-src` は最新と同内容(`desktop/` の差はドライブ文字固定をやめた意図的改変だけ)。`w64.cmd` の新機能(`:svchost` `:bcdfix` `tune.flag` `wifi_clean` の条件)もW4T版に入っている | **更新なし** |
| mesa-switch-woa | ruje0504-2/mesa-switch-woa | `68c4dd3` (2026-11-24) | 取り込み済みと同じ。`integration/switch-lite` は固定コミット `c77f8a6` から変更なし | **更新なし** |
| hayato41200/Windows-for-Switch | | `293bba2` | 取り込み済みと同じ | **更新なし** |

## 本zipの中身: SwitchPowerButton (Erista) 1.0.4.0 を取り込み
上流 1.0.4.0 の中身: 再起動(hekate へ R2P / PSCI)、PMIC による電源オフ、**高速スタートアップをドライバが切っておく**(これが無いと「シャットダウン」が休止になり電源が切れずログオン画面に戻る)。
W4T独自の修正(上流に無い)は残した: **長押しCtrl+Alt+Del(HoldSasMs / hold_sas.h)**、**スリープ停止・再開(SelfManagedIoSuspend/Restart)**。
方法: 基準=上流 `e6f0672`、W4T現状、上流最新 `15862ba` の三者マージ(8か所の衝突は全部「両方残す」で解決、DriverVersion の trace だけ上流の `DRIVER_VERSION`)。

| ファイル | 内容 |
|---|---|
| drivers/t210-icosa/drivers/SwitchPowerButton/src/switchpwrbtn.c | マージ後のドライバ本体 |
| .../switchpwrbtn.inf | DriverVer 11/25/2026,**1.0.4.1**(上流1.0.4.0と同じ版にならないよう .1 にした) |
| .../README.md, README-ja.md | 上流の説明 + W4T独自機能の節 |
| drivers/t210-icosa/kits/Switch-PowerButton/Reset.ps1 | 上流の版(`-Mode FastStartup`)。ログ先は `%SystemDrive%` |
| .../FastStartup-On.cmd, FastStartup-Off.cmd | 新規 |
| docs/DRIVER-INVENTORY.md, docs/SLEEP-AND-MISSING-DRIVERS-ja.md | 版の表記を 1.0.4.1 に |
| CREDITS.md | 変更記録を1行追加 |
| _REMOVED-FILES.txt | 不要になった `_SwitchPowerButton-upstream-1.0.4.0.patch` |

## 確認したこと / していないこと
- 確認済み: 括弧の対応、重複定義なし、`check_credits` / `check_abi_sync` / `check_hwid_matrix` / `check_no_proprietary` / `check_acpi_resources` が通る、`test_hold_sas.c` が通る。
- **未確認: WDKでのビルド、実機**。マージはC言語の手作業なので、まず Actions のビルドを見てください。
- 実機で見てほしい点: (1) スリープ→復帰で起こした押下が新しい押下にならないか、(2) 「シャットダウン」で電源が切れるか、(3) 再起動で hekate に戻るか、(4) `HoldSasMs=4000` の長押し。
- 気になる点: W4Tのスリープ停止(Suspend)と上流のD0Exit(シャットダウン時にタイマー停止)は両方タイマーを止めるが、順序は Suspend → D0Exit で衝突しない設計のはず(未検証)。

## ついでの注意
`check_aula_audit.py` の画像の件は、このzipには含めていません(前の話の続きなので、スクリプトと `LICENSE-AUDIT.md` を貼ってもらえれば別途作ります)。
