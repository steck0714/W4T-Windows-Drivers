# NVIDIA L4T 表示ドライバ(`drivers/video/tegra/dc` 全体)— 上流のままの参照コピー
上流: https://github.com/CTCaer/switch-l4t-kernel-nvidia  固定コミット `76e6d48970b451c242c20f298b8d63027836bb0b`
ライセンス: **GPL-2.0**（NVIDIA CORPORATION / CTCaer。全 151 ファイルの冒頭に許諾文。Makefile 5 本は表記なし）。全文はルートの `LICENSE`。
中身: DC(`dc.c`,`dc_reg.h`,`window.c`,`mode.c`)、DP(`dp*.c`,`dpaux*`,`dp_lt.c`=リンク学習)、SOR(`sor*.c`,`sor_regs.h`)、HDMI(`hdmi2.0.c`,`hdmi_reg.h`)、
EDID(`edid*.c`)、HPD(`hpd.c`)、DSI(`dsi*.c`)、`nvdisp/`、`t21x/`、`hdcp/` ほか。ファイルは**一切改変していない**。ビルド対象ではない。
`drivers/t210-icosa/drivers/SwitchDisplay/tests/test_dp_link_diff.py` が `dp.c` から `tegra_dc_dp_calc_config()` を抽出して移植版と照合する。
