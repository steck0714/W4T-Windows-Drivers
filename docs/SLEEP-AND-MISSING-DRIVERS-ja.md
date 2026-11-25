# スリープと不足ドライバ(HDMI など)の調査と現状

2026-11-17 時点。**ここに書いた「作った」は、ソース/静的検査/ホストテストまで。WDK でのビルドと実機動作は未確認。**

## 1. スリープ

### Microsoft の要件（根拠: [Modern Standby のプラットフォーム設計](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/platform-design-for-modern-standby)、[PEP](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/platform-extension-plug-ins--peps-)）
- Modern Standby(S0 低電力アイドル)には FADT の `ACPI_S0_LOW_POWER_IDLE` と、**SoC ベンダー提供の PEP が必須**。Intel 以外では inbox の PEP が無い。
- ハイバネートを持つ構成は ACPI TAD(時刻/アラーム)と `_BTP` も要る。
- Arm では ACPI の `_S3` 系は PSCI（`CPU_SUSPEND`/`SYSTEM_SUSPEND`）とファームウェア(ATF)側の SC7 対応が前提で、Windows 側からは見えない部分。
- PEP の DDI（`pepfx.h` / `pep_x.h`）自体は[公開されている](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_information)が、Tegra X1 の SC7 / クロック / 電源ドメインの契約が確定していないまま作ると、ブート開始型ドライバなので**起動不能の恐れがある**。

### 現在の状態（`tools/check_power_callbacks.py` の結果）
- 現行 DSDT に `_S3`/`_S4`/`_PR0`/`_PS0` 等のスリープ・電源リソースは無い。`_S0W` は `SWITCH_LITE_POWER_IRQ` 時の SWOA0601 だけ（「通常の S0 ウェイクで、プロセッサ PEP も SC7 も追加しない」とコメント済み）。
- KMDF 31 ドライバ(T210 15 + T214 16、診断/Hello を含む)のうち **D0Entry と D0Exit の両方を持つのは 5 個**（T210 の Bluetooth、T214 の Bluetooth / Fan / PowerButton / Sensor）。T214 Wi-Fi は D0Entry のみ、TegraGpu は D0Exit のみ。ただし検査は文字列検索で、GpioClx/SpbCx/SDPORT ミニポートのように別の仕組みで電源を扱うものは正しく拾えない。T210 の Battery / Fan / PowerButton / Rtc / Sensor / Touch / CpuClock / Gpio、T214 の Rtc / Touch / Gpio / I2c(D0Exit なし)などは未実装。
- **T214 の SwitchPowerButton(1.0.0.5)は、表示状態に応じた Power Down / Wake Up 切替、S0 アイドル、Sx ウェイク、PMU_EXT 割り込みを持つ新しい実装**。T210 の同ドライバ(1.0.0.2)は古い系統。同様に T214 の Fan(1.0.0.13)、Sensor(1404 行)、Battery(1.0.0.11)、Bluetooth(1.0.0.19)、Sdhc(0.4.0.1)が T210 より新しい。

### この版でやったこと / やっていないこと
| 項目 | 状態 |
|---|---|
| 電源コールバック検査 `tools/check_power_callbacks.py`（`--strict` で CI 化可） | 追加 |
| 全ドライバの D0/Sx 実装状況の一覧化（上記） | 追加 |
| PEP / SC7 / S3 | **作っていない**。上記の理由で、実機と SoC 側の仕様確認なしに出すと危険 |
| T214 の新しい実装を T210 へ移す | **未実施**。Erista と Lite は PMIC・電源接続が違う項目（Fan・Sensor・Battery）があり、ボードごとの検証なしに丸ごとコピーできない |

### 推奨する順序（小さく確実なものから）
1. T210 SwitchPowerButton を T214 1.0.0.5 の構造（button_state / irq_route / D0 コールバック）へ寄せ、`HoldSasMs`（長押し Ctrl+Alt+Del）を載せる。IRQ 経路は**押下のみ**で、長押し判定はポーリング経路でしか成立しない点に注意。(2026-11-19: T214 の switchpwrbtn は上流 `c5ae935`(1.0.0.8)に更新し、`irq_route.h` と D0 コールバックはビルド対象から外れた(`irq_route.h` などのファイルは `LEGACY.md` のとおり残してある)。ポーリング方式なので、この寄せ先は 1.0.0.8 の構造に読み替える。`CREDITS.md` 参照。)
2. D0Exit/D0Entry の無いドライバに、(a)I2C/レジスタ状態の保存復元、(b)進行中リクエストの完了、(c)ポーリングタイマーの停止/再開、を足す。`check_power_callbacks.py` の表を埋めていく。
3. S0 アイドル(`WdfDeviceAssignS0IdleSettings`)は Sdhc / I2c / Gpio など下位から。
4. ハイバネート(S4)は Windows 側ではストレージ(SDHC ミニポート)のダンプ経路とファームウェア(UEFI)の再開が前提。先に SD ミニポートの休止対応を確認する。
5. PEP は最後。ATF の SC7 エントリ/復帰と PMC の復帰要因を実機で確認してから設計する。

## 追補（2026-11-17・2 回目）: T210 のポーリング系ドライバに Dx 停止/再開を実装

根拠: KMDF は**ドライバ自身のタイマーを止めない**。`EvtDeviceSelfManagedIoSuspend`（D0 を出る前、まだ I/O できる）で止め、`EvtDeviceSelfManagedIoRestart` で再開するのが定石
（[Self-managed I/O](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/self-managed-i-o-device-states)）。
止めないと、D3 の I2C コントローラへ SPB 転送を投げ続ける。

| ドライバ(T210) | 版 | 実装 |
|---|---|---|
| SwitchFan | 1.0.0.3 | 停止: タイマー停止 + ファンを UEFI 既定 76 へ。再開: 失敗カウンタ 0、500 ms 後から再開 |
| SwitchBattery | 1.0.0.8 | 停止/再開: 2 本のタイマー。**Init の二重オープンと Cleanup の閉じ忘れも修正** |
| SwitchSensor | 1.0.0.2 | 停止: ALS/IMU を停止。再開: 起動時と同じ手順で初期化 |
| SwitchCpuClock | 1.0.0.4 | 停止: ガード停止 + 既定クロックへ。再開: 要求プロファイルを再適用 |
| SwitchPowerButton | 1.0.4.1 | 再開時に現在のレベルでホールド状態を作り直し、PMIC に溜まった ONOFFIRQ を捨てる（**起こした押下を新しい押下にしない**） |
| SwitchTouch | 1.0.0.5 | 停止/再開: 100 ms ポーリング |

- 全タイマーのコールバックに「再アーム後に `Stopping` を再確認して取り消す」を追加（停止処理との競合で止めたタイマーが復活していた）。
- `tools/check_power_callbacks.py` に SMIOSusp / SMIORest 列と「WDF タイマーでポーリングするのに Dx で止めていない」検査を追加。**T210 は 0 件**。
  残りは **T214 の CpuClock / Touch / Wi-Fi**（Lite 側。T210 と同一ソースなので同じ修正が使える。Lite は動作中なので触っていない）。
- **未対応・要確認**: Rtc / Gpio（状態保存復元が要るかは実機で）、Tegra210I2c の D0Exit、USB-C ホスト給電（OTG）中のスリープで VBUS が出たままになる件、
  タッチコントローラ自体の再初期化（スリープでコンフィグが消えるか未確認）。
- 本物のスリープ（S3 / Modern Standby）は FADT・PEP・ATF の SC7 が要り、**このリポジトリの範囲外**（上の §1 のとおり）。ここまでは「その時に壊れないための下地」。

## 2. HDMI / 外部ディスプレイ

- WinX の標準表示は UEFI の GOP フレームバッファを使う Microsoft Basic Display で、**Windows 側に表示コントローラードライバは存在しない**。ドック経由の HDMI 出力には T210 の DC / SOR1 / DisplayPort リンク学習(ドック内の DP→HDMI 変換)を駆動する **WDDM ディスプレイミニポート**が必要で、GPU の WDDM(KMD+UMD)と一体の作業になる（[WDDM 構成](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)）。
- ドック検出の USB-PD(BM92T36)は、Battery ドライバの ACPI 接続(I2C1 0x18)に既にある。別ドライバにすると 0x18 の排他接続が衝突するため作っていない。参照実装は `reference/l4t/hekate/bdk/power/bm92t36.c`。
- 次に必要なもの: (1) Erista/Mariko の DC・SOR・DP の初期化シーケンスの調査（hekate の `bdk/display`・Switchroot の `tegra-dc`/`dp` が出典候補。今回は同梱していない）、(2) ドックの HPD/ハブ(USB)検出、(3) GPU WDDM KMD への統合。いずれも実機が無いと検証不能。

## 3. その他の不足（優先度順の整理）
| 不足 | 理由 / 次の一手 |
|---|---|
| Joy-Con(UART/HID) | Tegra UART コントローラードライバが無い。hekate の `bdk/input/joycon.c` が手順の出典（同梱済み） |
| T210 の XUSB（USB3） | T214 の `TegraXusb` は 358 行の足場。NVIDIA XUSB ファームウェア（再配布しない）が要る |
| ruje 最新の NVDEC / 動画デコード ABI(`nvdec_abi.h`) | 取り込んでいない。GPU KMD(WDDM)の完成が先 |
| Iowa / Aula(Mariko)専用ドライバ | ボードごとの ACPI・電源・タッチ・パネルの検証が前提。Aula には windows-switch v0.8.0 の構成を `drivers/t214-aula/` に別枠で置いてある（WinX の SWOA 系ではない） |
| T214 側の ABI ヘッダ同期 | T214 は WDK が無く検証できないため据え置き |

## 4. ライセンス・出典
`CREDITS.md` と `reference/l4t/SOURCES.md` を参照。

## 追補（2026-11-18）: クレジット整備・OLED 分割・静的レビュー
- **クレジット**: SPDX / Copyright の無かった 39 ファイル(t210 / t214 のドライバ `.c/.cpp/.h`)に出典ブロックを追加。OLED の 4 本は DanilKhmyrov/windows-switch を出典・所有者として明記。
  個人の作者・著作権の行は上流に無いので作っていない。再発防止に `tools/check_credits.py`(120 ファイル検査、`run_host_tests.sh` に組み込み)。
- **OLED の分割**: `drivers/t214-aula/drivers/<ドライバ名>/`(SwitchHid・SwitchBattery・SwitchTouchRead・SwitchAgent・SwitchWifi・SwitchBluetooth・SwitchAudio)。
  上流のビルドスクリプトは上流の配置が前提なので、`layout.json` と `tools/aula_layout.py`(`--check` で上流と SHA-256 一致、`--materialize` で上流の形に復元)を足した。
- **テストの不具合を 1 件修正**: `t214-hoag/docs/hdh001/tests/test-ftm4.py` が古いパス(`drivers/SwitchLiteTouch/...`)を読んで失敗していた → `SwitchLiteTouch/src/...` に修正し通過。
- **静的レビュー(目視 + grep。WDK ビルド・実機は無し)**:
  - T210 の Dx 対応(Fan / Battery / Sensor / CpuClock / PowerButton / Touch): Suspend で `Stopping` を立ててタイマー停止、Restart で解除して再開、タイマー内の再アーム後の再確認(`WdfTimerStop(..., FALSE)`)まで、6 本とも同じ形で揃っている。Battery の二重オープン防止と Cleanup でのターゲット解放も読んで問題は見つからなかった。SwitchRtc はポーリングを持たないので対象外。
  - OLED の swhid / swbatt / touchread: `MmMapIoSpace` / `MmUnmapIoSpace`、`ObReference` / `ObDereference`、`ZwCreateFile` / `ZwClose` の対応を数え、swhid のマップ失敗時の分岐を読んだ。明らかな漏れは見つからなかった。**上流のファイルなので修正はしていない**(見つけたら上流へ報告)。
  - これは網羅的な監査ではない。タイミング競合・レジスタ手順は実機でしか確かめられない。
- **まだ無いもの**: HDMI(外部ディスプレイ)・スリープ(S0 低電力 / PEP / SC7)・Erista の GPU 以外の不足ドライバは、上の理由(仕様と実機の確認なしに作ると起動不能の恐れ)で**作っていない**。
