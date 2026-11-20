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

## 2026-11-20: fwfetch のドライブ文字まわりを修正(Switch の Windows は C: とは限らない)
- [x] `--deploy-gpu auto` / `--deploy-drivers auto` = 動いている Windows の `%SystemRoot%\System32\drivers[\TegraGpu]`。`%SystemRoot%` が無ければエラー(C: に決め打ちしない)。パスの `%VAR%` 展開(未定義はエラー)
- [x] `--auto` が、ドライブ文字の無いボリューム(FAT32 `switchroot` 領域など)も `mountvol` の一覧から `\\?\Volume{GUID}\` で探す。探索先の重複を大文字小文字・末尾 `\` 無視で除去
- [x] `Install-FwFetchStartup.ps1` はドライブ文字をタスクに書かない(`auto` で保存、スクリプトは実行時に `$env:ProgramData` から探す)。`FwFetchStartup.ps1` は古い文字のパスをいまの Windows に読み替え、存在しないドライブにはフォルダを作らずに止まる
- [x] Rust 単体テスト 5 件 + `run_tests.py` 5 件追加(Linux で全通過)。**Windows 実機・ARM64・PowerShell は未確認**。`build/*/fwfetch/` は CI が作り直す(この修正では触っていない)
- [x] 2026-11-20: `swagentsvc.c` / `swbatt.c` / `touchread.c` / `w64.cmd` / `tune.ps1` / `remove_win10.ps1` / `gm20bctl.c` の C: 固定を、実行中 Windows のシステムドライブ基準に変更。SD のブート領域は `kit/agent/findagent.cmd` が A〜Z → 文字なしボリューム(GUIDパス → 空き文字を実行時に選んで一時割り当て)の順に探す(Switchroot系の複数パーティションSDを想定、実機未確認・ビルド未実行)
- [x] 2026-11-20: `upstream-src/desktop` の boot/setup 系・w32/w64・各 ps1 も同様に変更(上流とは意図して不一致。`UPSTREAM-SOURCE.md` 参照)。boot.cmd / setup.cmd も findagent.cmd 経由(D〜K の固定リストをやめた)
- [ ] 未対応: `tools/testsign`・各 kits のログ(`C:\…`)
