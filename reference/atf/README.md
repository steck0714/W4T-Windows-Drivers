# ATF (Switchroot bl31) 参照コピー
| パス | 上流 | 固定コミット | ライセンス |
|---|---|---|---|
| `plat-nvidia-tegra/`（`plat/nvidia/tegra` 全体: soc/t210, common, include, drivers） | https://github.com/CTCaer/switch-l4t-atf | `ed2ef6bed942d06634ff2d065738542ea506c138` | BSD-3-Clause（ARM Limited / NVIDIA / CTCaer。各ファイル冒頭。全文 `LICENSE.rst`） |
| `plat_psci_handlers.c`, `tegra_def.h`（上のコピー。SwitchSleep のテストが参照） | 同 | 同 | 同 |
PSCI の電源状態 ID（コア停止 7 / クラスタ 16 / SoC(SC7) 27）と、SYSTEM_SUSPEND に `sc7entry-fw` が要ること、の根拠。ビルド対象ではない。
