# 静的監査メモ (2026-11-17)

**範囲:** 全ドライバに対する機械的なパターン検査と、Microsoft の資料との照合。人手で全行を読んだ監査ではない（並列で詳細監査を走らせる試みは利用上限で中断した）。WDK が無いのでビルドはしていない。

| 検査 | 結果 |
|---|---|
| `RtlQueryRegistryValues` の DIRECT に TYPECHECK が無い | 0 件（SwitchPowerButton で修正済み） |
| INF の `DriverVer` 形式 | 全 INF 正常 |
| `ExAllocatePool` / `ExAllocatePoolWithTag`（非推奨） | 0 件（`ExAllocatePool2` を使用） |
| `KeStallExecutionProcessor` が 50µs 超 | **Tegra210I2c(T210/T214) で最大 1000µs → 修正**（下記）。SwitchBluetooth は 100µs で、設計上の短い待ち。 |
| HWID と DSDT の突き合わせ | `check_hwid_matrix.py` 全通過 |
| D0Entry/D0Exit の有無 | `check_power_callbacks.py`。T210 側に未実装が多い（`SLEEP-AND-MISSING-DRIVERS-ja.md`） |

## 修正
- **Tegra210I2c 1.0.0.5(T210) / 1.0.0.6(T214):** SPB 転送間の `DelayInUs` を、50µs 超なら DISPATCH_LEVEL 未満で `KeDelayExecutionThread`、DISPATCH_LEVEL では従来どおり上限 1ms で stall に変更。根拠: [KeStallExecutionProcessor](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kestallexecutionprocessor) は長時間の使用を避けるべきとされている（目安 50µs）。ヘルパーのみスタブ付きで構文確認。ドライバ全体は未ビルド。

## 未実施
- 全ドライバの手読み監査（Sdhc / Wi-Fi / Bluetooth / Audio / XUSB / Battery など）。大きいので、ドライバ群ごとに分けて進めるのがよい。
- 各 D0Exit/D0Entry の追加（スリープ対応）。

---

## 追補（2 回目）: 実際にソースを読んで見つけたもの

方法: t210 の小型ドライバ（Fan / Battery / Sensor / CpuClock / PowerButton / Touch）の電源・タイマー・Init/Cleanup を読み、Microsoft の KMDF 資料（Self-managed I/O、WdfTimerStop）と照合。
t210 と t214 の差分、windows-switch v0.8.0 と SwitchDrivers-2026_11_16 の同梱物とも突き合わせた。**ビルドは未実施（WDK 無し）。**

| # | 内容 | 場所 | 対応 |
|---|---|---|---|
| 1 | ポーリングタイマーが Dx で止まらず、スリープ中も I2C を叩く | T210 の 6 ドライバ | `SelfManagedIoSuspend/Restart` を追加 |
| 2 | 「`Stopping` 確認→再アーム」の間に停止が入ると、止めたタイマーが復活する | 同 6 ドライバの全タイマー | 再アーム後に再確認して `WdfTimerStop(…, FALSE)` |
| 3 | 起こした電源ボタン押下／スリープ中に溜まった ONOFFIRQ が新しい押下になり、即スリープに戻りうる | SwitchPowerButton | Restart でレベルと IRQ を読み直す |
| 4 | Gauge/Charger を `SelfManagedIoInit` のたびに開き直し、ハンドルを漏らす。Cleanup で閉じない | SwitchBattery | NULL ガード + Cleanup で閉じる |
| 5 | BT 1.0.0.15: `BootPending` が以後の起動で BT を無効にし続ける、部分的にしか書けなかったパッチを公開しうる、D0 喪失後の UART-D CAR 未復元 | SwitchBluetooth(T210) | t214 の 1.0.0.19 へ更新（テスト通過） |
| 6 | `Build-Driver.ps1`: C++（Audio）を作れない、signtool/Inf2Cat のパスが 10.0.26100.0 固定、GitHub のシークレット PFX を無視して別の証明書を作る | `tools/Build-Driver.ps1` | 1 本に統合、自動検出、`-CertThumbprint` |
| 7 | ドライバごとに別証明書になる構成だと、1 つの `.cer` ではキットが通らない | CI | 全ドライバを 1 ジョブ・1 証明書でビルド |
| 8 | `build-erista-gpu.yml` が PowerShell の自動変数 `$args` を代入していた | CI | `$bargs` に変更 |
| 9 | 公開物: ライセンス全文（MIT / ISC / BSD-2-Clause-Patent）が無い、`.gitignore` に鍵が無い | ルート | `LICENSES/`、`COPYING.md`、`.gitignore` |

### 他の同梱物との突き合わせ
- **SwitchDrivers-2026_11_16**: Battery 1.0.0.6 / Bluetooth 1.0.0.13 / PowerButton 1.0.0.1 / nvtsdhc 0.3.2.0 は、ソース（1.0.0.7 / .15 / .2 / 0.3.2.1）より古いビルド。
  `Audio.ps1` と README はキット側が新しい（Check モード、ヘッドホン）ので取り込み済み。`Wlan.ps1` はキット側が古い（Lite の `switchwifi` まで消す）ので**取り込まない**。
  全 `.sys` は ARM64・署名済み・コード/データは常駐(NOT_PAGED)で、Build-Driver.ps1 の方針どおり。
- **windows-switch v0.8.0**（リリースノートに「実機確認は Mariko の Switch 有機EL(HEG-001) 1 台のみ」と明記。同梱ファイル自体に機種名は無い）: 同梱の音声 1.0.0.9 / BT 1.0.0.19 / Wi-Fi 0.5.7.0 の INF は、
  t214 の同名 INF と **日付と表示名以外が同一**（SWOA0C01 / 0B01 / 0D01）。つまり新しいドライバ本体を足すものではなく、winx Lite 系の再梱包。
  音声 1.0.0.8/.9 の差分は**診断トレース（コーデックのレジスタ/PCM のピーク）だけ**で動作は変わらないため T210 へは移していない。
  Wi-Fi の Erista 用パッケージ INF は 0.5.5 のままだったので、ビルド元のコア（0.5.7）に合わせて 0.5.7.0 にした（Erista は実機未検証のまま）。
  `swbatt.sys` / `swhid.sys` / `swagentsvc.exe` はバイナリのみでソースは `reference/windows-switch/`（GPL-2.0）にある範囲だけ。

### 追補（3 回目）: リリースノート(説明文)から反映したもの
出典: windows-switch v0.8.0 のリリースノートと、同梱 `agent\w64.cmd` の `:bcdfix`（実装を読んで確認）。

| # | 内容 | 対応 |
|---|---|---|
| 10 | **`bcdedit` が「要求されたシステム デバイスが見つかりません」で失敗**（言語パックや起動ファイルのサービスも失敗）。原因は 2 つ: (a) MBR ではシステムパーティションが**アクティブ**でないとカーネルが受け付けない（`SiValidateSystemPartition` はブート インジケータを見る）、(b) BCD に `Description\System` と `TreatAsSystem` = 1 が無い（BCD-Template 由来で起こる） | `Stage0-SwitchSetup.ps1 -Mode Prepare` に `Repair-BcdSystemStore` を追加（`mountvol S: /S` → BCD を `reg load` → 2 値を 1 に → `reg unload`）。(a) は Windows の外からしか直せないので、メッセージと README に手順（MBR の 446 バイト目を 0x80）を記載。**未実機** |
| 11 | PMIC の I2C 転送が 1 回失敗しただけで GPU 電源投入が失敗する（I2C5 は電池・ファン・充電と共有） | TegraGpu `windows/platform_io.c` の `Spb()` を最大 3 回・1 ms 間隔のリトライに。失敗は最後の 1 件だけ `IoStatus` に記録（一時的な失敗で汚さない）。レジスタ書き込み/読み出しは冪等なので安全。windows-switch の swbatt「GPU 電源ブリッジが PMIC I2C 転送をリトライ」と同じ方針。**未ビルド** |
| 12 | Erista Wi-Fi パッケージ INF が 0.5.5 のまま（コアは 0.5.7。0.5.7 は切断タイムアウトから回復する） | INF を 0.5.7.0 に |

- 取り込まなかったもの: swbatt の「新しい GPU ブリッジ要求（DVFS ポリシー固定）」と GPU クロック/電圧のログ（Erista の KMD は swbatt を経由せず SPB で直接 PMIC に触るため対象外）、Lite 化 `tune.ps1`、起動時間短縮（シム 4 秒待ち・swagentsvc）、Windows Update の手動起動化（いずれも windows-switch の agent / bl33 側）。
- 同リリースノートでも、**GPU デスクトップ（mesa-switch-woa）は NVIDIA ファームウェアが要るためアーカイブに含めず README 参照**とされている。Wi-Fi ファームウェア・BT パッチ・Inf2Cat も「再配布できない」。本リポジトリの扱い（含めない）と一致。
