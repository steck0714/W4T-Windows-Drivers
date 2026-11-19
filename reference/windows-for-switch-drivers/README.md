# ruje0504-2/Windows-for-Switch-Drivers の `core_voltage.h` — 上流のままの参照コピー
上流: https://github.com/ruje0504-2/Windows-for-Switch-Drivers  固定コミット `c5ae935b59c6229db4be23c561d31088503f40e4`  ライセンス: **MIT**（ファイル先頭の `SPDX-License-Identifier: MIT`。このファイルの許諾はこの行に従う）。
`drivers/SwitchPowerButton/src/core_voltage.h` の1ファイルだけ（上流のパスのまま。リポジトリ全体ではない）。MAX77620 を持つドライバ（switchpwrbtn）が作る `\Device\SwitchCoreVoltage` への IOCTL 契約（`IOCTL_SWCORE_EMC_VOTE`、`SWCORE_VOTE`）で、`drivers/t214-hoag/SwitchFan/src/t214/emc_windows.h` が `#include` する。
ビルドに使うコピーは `drivers/t214-hoag/SwitchPowerButton/src/core_voltage.h`（このファイルと同一）。

- SHA-256: `3cbca25f9f93b4add4f7eb3123dc12d81e05e250f586e3f22b985f54b57c77e4`（上流・参照コピー・`drivers/t214-hoag/` のコピーで同じ）
- 上流のルートの `LICENSE`（GPL-2.0）はこのファイルの許諾ではないので同梱しない。MIT 全文は `LICENSES/MIT.txt`。出典は `CREDITS.md`。
- 上流の switchpwrbtn（1.0.0.7）の `core_voltage_control.h` など、この1ファイル以外は同梱していない。
