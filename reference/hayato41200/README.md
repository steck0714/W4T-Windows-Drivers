# hayato41200 の Windows-for-Switch / Windows-for-Switch-Drivers — 上流のままのスナップショット

取得日: 2026-11-27(初回 2026-11-20 から差分更新。11-26 の SwitchCenter 0.9.3 / SwitchPatcher 0.6.1 / R448H の GTDT を反映)(`git clone --depth 1`)。ライセンス: **GPL-2.0**(各リポジトリのルートの `LICENSE` を同梱。ファイルごとの SPDX / 著作権表示が優先)。**無改変**(`.git` だけ除く)。
このスナップショットは、上流が非公開になっても参照できるように置いたもの。**ビルドには使わない**(ビルド対象は `drivers/`)。

| ディレクトリ | 上流 | 固定コミット |
|---|---|---|
| `Windows-for-Switch/` | https://github.com/hayato41200/Windows-for-Switch(UEFI / DSDT、Erista) | `9f315d9a2a1dcf25fb8f727f1765a9ecf7198008` |
| `Windows-for-Switch-Drivers/` | https://github.com/hayato41200/Windows-for-Switch-Drivers(ドライバ / SwitchCenter / SwitchPatcher / kits) | `7eebea099eb0fe97c75c8066d0188bd2ff120c5d` |

- 全ファイルの SHA-256 は `MANIFEST.sha256`(このファイルと `MANIFEST.sha256` 自身を除く)。
- 参考にしたい所: `Windows-for-Switch-Drivers/kits/Switch-Gpu/`(段階 A〜B3c の実機結果と `COMPARE-B3c-ja.md`)、`drivers/SwitchGpu/`、`docs/`。
- 再配布用の写し(kits とビルド済み EFI)は `build/HAC-001/hayato41200/`。出典と注意は同フォルダの `README.md`。
- 出典は `CREDITS.md`、ライセンスの扱いは `COPYING.md`。
