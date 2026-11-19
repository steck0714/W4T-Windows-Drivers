# HAC-001-01 — 2019 年版（Mariko / Tegra X1+、コードネーム Iowa）

**このリポジトリには、Iowa 用のドライバ一式が無い。** そのため、ドライバ（`.sys`）は置いていない
（CI でもビルド対象外。`build/models.json` は `build: none`）。テスト署名用のスクリプトだけは、
`build-all-drivers` が `build/HAC-001-01/test-signing/` に作る（`.cer` なし、署名するドライバが無いため）。

- `drivers/t210-icosa/`（Erista）も `drivers/t214-hoag/`（Lite）も、Iowa の基板・電源・I2C・SD 配線と一致する証拠が無い。
  流用して「動く」とは書けないので、流用しない（`docs/DRIVER-INVENTORY.md`: Iowa は専用の枠なし）。
- Iowa で動作報告のある配布物があれば、HEG-001 の上流バイナリ方式（`heg001_upstream`、`tools/build/fill_heg001.py`）と同じ要領で、
  ハッシュ付きの一覧を作ってから置く。