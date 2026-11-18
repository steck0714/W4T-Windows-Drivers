# HDMI(ドック出力)設計 — Erista / T210

2026-11-18。**確認済みは「L4T ソースとの照合・ホスト単体テスト」まで。レジスタ書き込み・Windows 表示・実機は未確認。**

## 1. 前提(根拠つき)
- Switch のドック出力は HDMI 端子そのものではなく、**USB-C の DP オルタネートモード → ドック内 DP→HDMI 変換**。SoC 側は **DC(表示ヘッド)→ SOR1 → DPAUX1 → DP レーン**。L4T の実装は `dp.c`(3674 行)/`dp_lt.c`(リンク学習)/`dpaux.c`/`sor.c`/`edid.c`/`hpd.c`(`CTCaer/switch-l4t-kernel-nvidia` `76e6d489`)。
- ドック検出は BM92T36(USB-PD)。DT では `extcon-cables = <&bm92t 0/1>` で USB ホスト/DP の 2 種を通知(`reference/l4t/platform-t210-nx/.../tegra210b01-nx-common.dtsi:650-673`)。このリポジトリでは BM92T36 は Battery ドライバが I2C1 0x18 を排他で持つ(`SLEEP-AND-MISSING` §2)。
- **hekate の `bdk/display` は内蔵 DSI パネル専用**(DP/HDMI/SOR の記述なし、`di.c` を確認)。つまり UEFI/EDK2 側にも HDMI 初期化の既存実装は無い。

## 2. 経路の選択(重要)
| 経路 | 内容 | 規模 | 判断 |
|---|---|---|---|
| **A: UEFI で DC/SOR1/DP を初期化し GOP をドック側に向ける** | Windows は Microsoft Basic Display で **GOP フレームバッファ**をそのまま表示。WDDM 不要。ドックを挿して起動すれば HDMI に出る。ホットプラグ切替は不可(再起動) | bl33(EDK2)に DPAUX/リンク学習/SOR/DC のみ | **最短。先にこれ** |
| B: WDDM ディスプレイミニポート | ホットプラグ・解像度変更・GPU 描画と連携 | GPU KMD と一体で巨大 | A が動いた後 |

## 3. A の手順と L4T の対応(`reference/` 外の上流ファイル、行は `76e6d489`)
1. ドック検出(PD の DP 通知) → HPD 待ち。`dp.c` `tegra_dp_wait_for_typec_connect` / `hpd.c`
2. DPAUX 有効化、DPCD 読み(`tegra_dp_dpaux_enable` 984、`tegra_dp_dpcd_init` 2339、`tegra_dp_read_sink_cap` 2423)
3. EDID 読み(AUX の I2C-over-AUX、`tegra_dc_dp_i2c_xfer` 161、`edid.c`)→ モード選択
4. **リンク設定**: `tegra_dc_dp_calc_config` 1022 = **本リポジトリで移植・検証済み**(`SwitchDisplay/src/dp_link.h`)
5. SOR 電源/パッド: `tegra_sor_dp_pad_power_up` 1344、`tegra_sor_power_lanes` 1107、`tegra_sor_cal` 1744、`tegra_sor_config_xbar` 1787、PLLDP/クロック `tegra_sor_config_dp_clk_t21x` 239
6. **リンク学習**(CR → EQ、電圧スイング/プリエンファシス): `dp_lt.c`(1135 行)、`tegra_dp_tu_config` 2373 で TU 値を SOR に書く
7. `tegra_dc_sor_attach` 1843 → DC ウィンドウ設定 → GOP フレームバッファのスキャンアウト先を DC1 に切替

## 4. 今回できたこと
| 項目 | 状態 |
|---|---|
| `dp_link.h`: `tegra_dc_dp_calc_config` の移植 + 最小帯域のリンク選択 `dp_pick_link` | **L4T 原本を抽出してコンパイルし、2160 通り(6 クロック×5 モード×3 レート×3 レーン×4 bpp×2)で全フィールド一致**(`tests/test_dp_link_diff.py`、venv で実行) |
| 1080p60 24bpp の選択結果 | RBR(1.62G)×4 レーン、TU=64、watermark=18(モデル上の値。実機で学習が通るかは別) |
| レジスタ書き込み(DPAUX/SOR/DC)・リンク学習 | **未実装**。ベースアドレスは T210 TRM 由来で未照合(DC1 0x54240000 / SOR1 0x54580000 / DPAUX1 0x545C0000 — 要確認) |
| EDID パーサ・モード選択 | 未着手(次) |

## 5. 実機で取ってほしいデータ(次の段に必要)
1. 同じ SD で Switchroot(L4T)をドックに挿して起動し、`dmesg | grep -i "dp\|sor\|hpd\|link"` と `/sys/kernel/debug/tegra_dp*`(学習後のレーン数・レート・ドライブ電流)。
2. そのときの EDID(`/sys/class/drm/*/edid`)と、ドックの種類(純正/社外)。
3. UEFI シェルで、ドック接続状態の BM92T36 ステータス(I2C1 0x18)。
4. Windows 起動中に HDMI 抜き差ししたときの挙動(A 経路ではホットプラグ非対応のため、画面が固まるか確認)。
