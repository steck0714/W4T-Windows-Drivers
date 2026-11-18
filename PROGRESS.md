# 作業進捗(トークン切れ対策: 段階ごとに保存。再開はこのファイルを読む)

## 方針
スリープ → HDMI の順。各段階を「調査→小さく実装→実コンパイル/ホストテスト→ここに記録」で閉じる。
検証環境: venv(`python3 -m venv`)、iasl(ACPICA をビルド)、gcc。WDK は無い。

## 完了
- [x] 現状把握(README / SLEEP-AND-MISSING)
- [x] 参照取得: switch-l4t-atf、hekate l4t.c、platform-t210-nx(DT)
- [x] S1 `acpi/t210/lpi.asl.inc` + DSDT フック(`SWITCH_LPI`)。`tools/check_dsdt_compile.py` で 3 構成 PASS
- [x] S2 `drivers/t210-icosa/drivers/SwitchSleep/`(PSCI ヘルパ + テスト)。`reference/atf/` に根拠コピー
- [x] `docs/SLEEP-DESIGN-ja.md`

## 完了(HDMI 前半・2026-11-18)
- [x] H0 参照取得: `CTCaer/switch-l4t-kernel-nvidia`(DC/SOR/DP 一式。取得可)。`reference/nvidia-dc/dp.c` に原本コピー
- [x] H1 `docs/HDMI-DESIGN-ja.md`(経路A: UEFI で GOP をドック側へ / 経路B: WDDM)
- [x] H2a `SwitchDisplay/src/dp_link.h` + L4T 原本との差分テスト(2160 通り一致)

## 完了(2026-11-18 追加: 不足点の点検)
- [x] `docs/GAPS-2026-11-18-ja.md`。再配布OKの上流を `reference/` に無改変で取り込み(nvidia-dc / atf / hekate-l4t / ms-pep)
- [x] fwfetch: `sc7entry` `sc7exit` `xusb_t210` を追加、Erista プロファイルに bl31/XUSB も追加(合成データのテスト追加、全PASS)

## 次
- [ ] H2b EDID パーサ + モード選択(ホストテスト)
- [ ] H2c DPAUX トランザクション層(模擬シンクでテスト)、リンク学習ステートマシン(模擬シンクでテスト。レジスタ層は抽象化)
- [ ] H2d SOR/DC のレジスタ手順(L4T `sor.c`/`dp_lt.c` から表を起こす)。ベースアドレスを一次資料で照合
- [ ] S3 PEP スケルトン(実機でブート確認できてから)

## 旧チェックリスト
- [ ] H0 参照取得: hekate `bdk/display`、L4T nvdisplay(公開リポジトリ名の確認が必要。CTCaer/switch-l4t-kernel-nvdisplay は取得不可)
- [ ] H1 設計書 `docs/HDMI-DESIGN-ja.md`(DC/SOR1/DPAUX、ドック検出 BM92T36、HPD、EDID、リンク学習)
- [ ] H2 ホストテスト可能な部分(EDID パーサ、DP リンク設定計算)を実装
- [ ] H3 実機で取るデータの一覧

## 注意
- 取得できなかったもの: switchroot/* の一部と CTCaer/switch-l4t-kernel-nvdisplay(認証要求)。
- 配布不可: NVIDIA/Broadcom ファーム、bl31.bin、sc7entry/exit.bin。
