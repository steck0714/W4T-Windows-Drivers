# スリープ設計(Erista / T210)— 根拠つき

2026-11-18。**ここに書いた確認は「ソース照合・iaslコンパイル・ホスト単体テスト」まで。Windows での動作・実機は未確認。**

## 1. 参照実装から確定した事実
| 事実 | 根拠(固定コミット) |
|---|---|
| bl31 の PSCI 電源状態 ID は コア停止=7 / クラスタ=16 / SoC(SC7)=27。形式は拡張 state id(type=bit30)なので `0x40000007` / `0x40000010` / `0x4000001B` | `reference/atf/tegra_def.h`、`platform.mk: PSCI_EXTENDED_STATE_ID=1`(CTCaer/switch-l4t-atf `ed2ef6be`) |
| SC7(SYSTEM_SUSPEND)は、BPMP-FW が無い場合 **`sc7entry-fw` が bl31 に渡されていないと `NOT_SUPPORTED`** | `reference/atf/plat_psci_handlers.c` の `PSTATE_ID_SOC_POWERDN` |
| hekate の L4T 起動は `sc7entry.bin` / `sc7exit.bin` を SD から読み、`sc7entry_fw_base/size` を bl31 に渡す(T210 では BPMP-FW は使わない) | CTCaer/hekate `bootloader/l4t/l4t.c`(`8e1392df`)。現在の起動経路(hekate l4t=1)は**既に条件を満たす可能性が高い** |
| Switchroot の NX 用 DT は `idle-states/c7` を **disabled** にしている(Switch ではコア停止アイドルを使っていない) | `reference/l4t/platform-t210-nx/.../tegra210-odin-common.dtsi:1132` |
| WinX の DSDT には CPU0–3(ACPI0007)はあるが `_LPI` / `_S3` / PEP 用の記述は無かった | `acpi/Dsdt.winx-usually.asl` |

## 2. Windows 側の制約(未解決の部分)
- Windows の ARM 機は S3 ではなく Modern Standby(S0 低電力アイドル)を前提とし、SoC の最深アイドルは **SoC ベンダー提供の PEP** が担う(`docs/SLEEP-AND-MISSING-DRIVERS-ja.md` §1)。Surface RT(Tegra 3)が Windows RT で公式に動いていたのも、NVIDIA 製の PEP を含むこの方式。**Tegra X1 向けの PEP は公開されていない。**
- したがって「SC7 に落とす」には最終的に PEP(ブート開始型カーネルドライバ)が必要。作る場合の契約: ①他コアを PSCI CPU_OFF で止める ②再開エントリ(物理アドレス)を渡して `SYSTEM_SUSPEND_64` ③復帰後に全デバイスが D0 に戻る。①②の前提検査は `SwitchSleep/src/psci_t210.h` に実装(ホストテスト済み)。

## 3. 段階(小さく確実なものから)
| 段 | 内容 | 状態 |
|---|---|---|
| S0 | Dx 時にポーリング停止(Fan/Battery/Sensor/CpuClock/PowerButton/Touch) | 実装済み(前回)。静的検査のみ |
| S1 | **CPU `_LPI`**: WFI=有効、コア停止(0x40000007)=**無効フラグ付き**(上記 DT の根拠)。`SWITCH_LPI` で opt-in、`SWITCH_LPI_C7_ENABLE` で有効化試験 | **今回追加。iasl で 3 構成コンパイル OK**。Windows が `_LPI` を消費するかは未確認 |
| S2 | PSCI ヘルパ(ID 検証・SC7 前提検査) | **今回追加。ATF コピーと突き合わせ、C テスト OK** |
| S3 | PEP スケルトン(何もしない版。登録・ログのみ。レジストリでキルスイッチ) | **未着手**。ブート開始型なので、実機でブート確認してから次へ |
| S4 | PEP: プラットフォーム最深アイドルで S2 の手順を実行 | 未着手。実機の SC7 復帰ログ(hekate/UART)が要る |

## 4. 実機で取ってほしいデータ(次の段に必要)
1. `SWITCH_LPI` あり/なしで起動し、`powercfg /energy` と `Get-WmiObject` ではなく **デバイスマネージャの「プロセッサ」→ 電源**表示、アイドル時の消費電力(USB 電流計)。
2. 同じ SD で Switchroot(L4T)を起動し `echo mem > /sys/power/state` で SC7 が通るか(`sc7entry.bin` / `sc7exit.bin` の有無も)。通れば bl31 側は OK。
3. UEFI シェルから SMC `PSCI_FEATURES(SYSTEM_SUSPEND)` の戻り値(0 なら対応)。
