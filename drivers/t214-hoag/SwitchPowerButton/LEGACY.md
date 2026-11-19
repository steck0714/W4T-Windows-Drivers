# SwitchPowerButton — 旧版(DriverVersion 5)から残しているファイル
このフォルダのドライバは上流 ruje0504-2/Windows-for-Switch-Drivers `c5ae935`(DriverVersion 8 = INF 1.0.0.8、S0 のポーリング方式)。
上流に無い次の旧版のファイルは、参考用に**残してあるだけで、ビルドにも CI にも使わない**。

| ファイル | 中身 | 今の状態 |
|---|---|---|
| `src/irq_route.h` | PMIC/GIC の wake 割り込みの経路(マスクの保存と復元) | どこからも `#include` されない |
| `tests/test_irq_route.c` | `irq_route.h` 単体のテスト | 単体で今も PASS（`clang tests/test_irq_route.c`） |
| `tests/test-wake-callbacks.py` | 旧 `switchpwrbtn.c` から `SwPwrIrqRead` などを抜き出して試す | **1.0.0.8 では失敗する**(その関数が無い)。旧版のソースを取り出したときだけ使う |

- ビルド: `tools/ci/drivers-t214-hoag.json` の `switchpwrbtn` は `src/switchpwrbtn.c` だけを渡す。旧ファイルは読まれない。
- テスト: `tools/run_host_tests.sh` は `drivers/t210-icosa/drivers` だけを走査する。ここの `tests/` は走らない。
- 上流の説明: DriverVersion 8 は「S0 のポーリング。PMIC/GIC の wake 割り込みと SC7 は実装しない」。経緯は `CREDITS.md` の 2026-11-19 の行。
- 旧版の `switchpwrbtn.c` は git 履歴にある。
