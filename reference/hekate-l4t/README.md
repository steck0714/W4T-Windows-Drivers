# hekate の L4T 起動部(`bootloader/l4t`)と表示(`bdk/display`)— 上流のままの参照コピー
上流: https://github.com/CTCaer/hekate  固定コミット `8e1392df4569ac501a115867056b13c361429e9e`  ライセンス: GPL-2.0（`LICENSE`。各ファイルの著作権表示は CTCaer ほか）。
`l4t.c` が `bootloader/sys/l4t/` から `sc7entry.bin` / `sc7exit.bin` / `bpmpfw.bin` を読み、bl31 に渡す手順の一次資料。`bdk/display` は内蔵 DSI パネルのみ（DP/HDMI なし）。
