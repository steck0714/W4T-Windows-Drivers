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
