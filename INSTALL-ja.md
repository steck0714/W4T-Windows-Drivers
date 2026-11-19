# w4t-switchfan-v33-changes の入れ方

## 1. 展開
このzipは、W4T-Windows-Drivers の `test` ブランチのルートに展開します。パスは `test` ブランチと同じです。上書きしてかまいません。
削除するファイルはありません。

## 2. 中身
- 新規 42 / 修正 17 ファイル: `drivers/t214-hoag/SwitchFan/`(ruje0504-2/Windows-for-Switch-Drivers c5ae935、DriverVersion 33)、
  `.github/workflows/`、`CREDITS.md`、`.gitignore`、`tools/ci/`、`drivers/t214-hoag/TegraGpu/tools/build.py`、`drivers/t214-hoag/docs/hdh001/build-drivers.sh`
- `external/mesa-switch-woa/tegra-x1-wddm/`: SwitchFan が必要とする mesa-switch-woa(ruje0504-2、コミット c77f8a61fd4b627b218fee1db34b654b4e7233a2)のヘッダと、そのライセンス(GPL-2.0-only / MIT)。
  再配布する場合は、この `LICENSE.md`・`NOTICE`・`LICENSES/` を一緒に置いてください。
  - ビルドに必要な分だけの抜き出しです(ヘッダ6本)。mesa-switch-woa の全体ではありません。

- OLED(Aula)側: `tools/check_swbatt_bridge.py`(新規)、`drivers/t214-aula/README.md`、`build-aula-gpu.yml`、`build-all-drivers.yml` の変更。
  OLED のファンと GPU 電源ブリッジは swbatt.sys が担当し、Lite の SwitchFan は入れていません。`swbatt.c` は変更していません。
  swbatt が実装しているのは `0x100`〜`0x108`で、`0x109`〜`0x10f` は未実装です(README に書いてあります)。

## 3. external/ の扱い
- `.gitignore` に `external/` を足したので、そのままでは git に入りません。コミットに含めたいときは `git add -f external/`。
- CI(build-all-drivers.yml / build-lite-gpu.yml)は、同じ場所 `external/mesa-switch-woa` に、`tools/ci/mesa-switch-woa.rev` のコミットを自分で取得します。
  CI だけで使うなら、この `external/` は入れなくて大丈夫です(入れても CI が作り直します)。
- 手元(オフライン)で作るとき:
  - `python drivers/t214-hoag/TegraGpu/tools/build.py ... --probe --woa external/mesa-switch-woa`
    (`--woa` は git のチェックアウトで、コミットの一致を確認します。抜き出し版では通りません。フルの取得を使ってください)
  - `WOA_INCLUDE=external/mesa-switch-woa/tegra-x1-wddm sh drivers/t214-hoag/docs/hdh001/build-drivers.sh <src> <out>`
    (こちらは抜き出し版でそのまま使えます)

## 4. 確認状況
check_credits / check_hwid_matrix / check_abi_sync / check_no_proprietary / inf_scan、SwitchFan のホストテスト 17 本と test-screenoff.py は通っています。
ARM64 の WDK ビルドと実機は未確認です(CI で確認してください)。
