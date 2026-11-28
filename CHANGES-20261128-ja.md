# W4T 更新(2026-11-28)— 変更・新規ファイルのみ(削除なし)

基準: `steck0714/W4T-Windows-Drivers` の `test` ブランチ `0b27e8f`(2026-11-26)。
使い方: リポジトリのルートに展開して上書きし、コミットしてください。

## 上流の取り込み
- ruje0504-2/Windows-for-Switch-Drivers `c5ae935`→`de636f5`: SwitchFan 1.0.0.34、Tegra210I2c 1.0.0.8(I2C5 DFLL の arb-lost リトライ、D0 の温度窓)。
- ruje0504-2/mesa-switch-woa: 固定 `c77f8a6`→`1dbc9e57370bf4fb2e51f2d39e99280ed8a223f0`(`tgd3d-vulkan` の先頭)。
  `main` は SwitchFan が要る emc_gpu_vote.h / host1x_clock.h が無いので固定先にしない。
  `external/…/gpu_pmic.h` を更新。`reference/ruje-0504-2/mesa-switch-woa` を `1dbc9e5` に差分更新。
- hayato41200/Windows-for-Switch-Drivers `974e76a`→`7eebea0`: SwitchCenter 0.9.3 / SwitchPatcher 0.6.1(app と kit、リリースノート)。
- hayato41200/Windows-for-Switch `293bba2`→`9f315d9`: reference のみ(GTDT、package-R448H.sh)。

## TDR 修正の移植(上流 244d138 相当。W4T の構造に合わせて手で移植)
- `drivers/t214-hoag/TegraGpu/windows/adapter_reset.c`、`drivers/t210-icosa/drivers/TegraGpu/windows/adapter_reset.c`:
  再起動前に `Platform.IoStatus` を消す。再起動後に IoStatus が失敗なら Suspend/Reset で巻き戻す。
- `drivers/t214-hoag/TegraGpu/windows/platform_io.c`: preflight が DEVICE_NOT_READY のとき 50 ms × 最大 12 回リトライ
  (IoStatus は最終失敗だけ記録。温度 80 ℃ 以上は再試行しない)。Lite と有機 EL(Aula)が使う。
  Erista は SPB 直結で SwitchFan ブリッジを使わないため platform_io.c は変更なし。
- `drivers/t214-hoag/TegraGpu/tests/test_platform_windows.c`: 上記のテストを追加(Windows 用 PE のため未実行)。
- SwitchFan 側の「温度をその場で取得」は gpu_pmic.h の更新で入る。

## GPU の出力先 TegraGpu
- `.github/workflows/build-all-drivers.yml`: `commit-build` が gpu / gpu-lite / gpu-aula を待ち、
  `build/<型番>/drivers/TegraGpu/`(HAC-001, HDH-001, HEG-001)に .sys .inf .cat .cer .dll .lib と SHA256SUMS.txt を置く。
  GPU ジョブが失敗した回は前回分を残す。鍵とファームは `check_no_proprietary.py` が拒否する。

## ビルド入力の欠落チェック(新規)
- `tools/check_build_inputs.py`: ソース / INF / #include(再帰)/ キット / ブリッジヘッダの有無を検査。
  `tools/run_host_tests.sh` と CI の host ジョブに登録。

## 確認できたこと / できていないこと
- OK: check_build_inputs(欠落 0、注意 5)、check_abi_sync(全一致)、check_swbatt_bridge、check_no_proprietary、
  SwitchFan の 2 テスト、Tegra210I2c test-arb-lost(CC=clang)、TegraGpu コピー処理の模擬実行。
- 未実行: WDK でのビルド、TegraGpu の Windows 用テスト(test_platform_windows)、実機。
- 未対応: 上流 tegra-x1-wddm(完全な WDDM アダプタ)への置き換え。上流自身が不安定と記載しているため見送り。
