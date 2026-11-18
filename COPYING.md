# ライセンスの案内 / Licensing map

ルートの `LICENSE` は GPL-2.0(全文)。ただしファイルごとの `SPDX-License-Identifier` と各ディレクトリの `NOTICE` が優先する
（MIT / ISC / BSD-2-Clause-Patent のファイルが混ざっているため）。全文は `LICENSES/`、出典・作者は `CREDITS.md`。

| 場所 | ライセンス | 由来 / 備考 |
|---|---|---|
| `drivers/t210-icosa/`, `drivers/t214-hoag/` | 各ファイルの SPDX（MIT / GPL-2.0-only / ISC / BSD-2-Clause-Patent）。無印は各ディレクトリの `LICENSE`(GPL-2.0) | winx 由来（hayato41200 / ruje0504-2 / CTCaer ほか） |
| `drivers/*/TegraGpu/` | MIT と **GPL-2.0-only の混在** | `platform/t210_power.c` などは GPL-2.0-only。**リンクして作った `.sys` は全体として GPL-2.0-only で配る**（対応ソース＝このリポジトリの該当コミットを一緒に提供する） |
| `drivers/t214-hoag/SwitchWifi/core/` | ISC（Broadcom brcmfmac 由来）+ 一部 MIT | `NOTICE` を一緒に配る |
| `drivers/t210-icosa/gpu/GM20B-Diagnostic/regs/nvgpu/` | MIT（NVIDIA） | 各ファイル先頭の著作権表示・許諾文を消さない |
| `reference/l4t/` | hekate・platform-t210-nx・Linux 由来は GPL-2.0、nvgpu の gm20b は MIT | 上流のままコピー（`MANIFEST.sha256`、`NOTICE.md`） |
| `reference/windows-switch/` | GPL-2.0 | DanilKhmyrov/windows-switch の抜粋 |
| `drivers/t214-aula/` | GPL-2.0（windows-switch 由来、`LICENSE` 同梱） | v0.8.0 リリース zip のテキスト部分のみ。INF の出自（hayato41200 / ruje0504-2）のファイル別ライセンスは**未確認** |
| `tools/fwfetch/`, `tools/build/`, `tools/check_no_proprietary.py` | GPL-2.0-only（SPDX 表記） | 新規作成。fwfetch の Rust 依存は MIT で使う（`tools/fwfetch/THIRD-PARTY.md`） |
| `build/` | 中身のソースに従う（上と同じ）。`build/HEG-001/` は windows-switch 由来（GPL-2.0） | ビルド済みバイナリ置き場（`build/README.md`） |
| `acpi/`, `docs/`, `tools/` | ルートの `LICENSE` | |

## 再配布しないもの（確認済みの理由つき）
- **NVIDIA の GPU ファームウェア**（`NETB_img.bin` ほか 9 ファイル）: linux-firmware の `LICENCE.nvidia` 2.1.2 が、再配布を
  「OSI 承認のオープンソースライセンスで配布される OS 上での使用に限る」と定めている。**Windows 向けパッケージはこれに当たらない。**
  利用者が自分の Switchroot の SD から `tools/fwfetch/` で取り出す（リポジトリ外に出力。CI に置く場合は非公開の `NVIDIA_FW_URL` のみ）。
- **Broadcom の Wi-Fi ファームウェア / Bluetooth パッチ**: windows-switch v0.8.0 の同梱ドライバ自身が「ここでは再配布できない」としている。
- ~~ビルド済み `.sys` / `.cat`~~ → **2026-11-18 から、作者の許可で `build/<型番>/` に置いてよい**（`.exe` も可）。未検証・テスト署名であることを `build/README.md` と `BUILD-INFO.md` に書く。`build/` の外には置かない。
- **テスト署名の秘密鍵**（`.pfx` / `.pvk`）: 絶対にコミット・リリースしない（`.gitignore` で除外）。公開してよいのは `SwitchWOA-Test.cer` のみ。
  このリポジトリの `.cer` を Windows の信頼済みルートに入れる運用なので、秘密鍵が漏れると**その Switch で誰でもドライバを署名できてしまう**。

## ビルド済みバイナリを置くとき
- 置いてよいのは `build/<型番>/` の中だけ。`MANIFEST.json` と `SHA256SUMS.txt` を付ける（`tools/build/assemble_build.py` が作る）。
- **秘密鍵（`.pfx` など）は置かない**（`.gitignore` と `assemble_build.py` と `check_no_proprietary.py` の 3 か所で止める）。公開してよいのは `.cer` だけ。
- 再配布できないファームウェア（NVIDIA / Broadcom / `bl31.bin`）は、バイナリ解禁後も置かない。
- 他人のビルド済み（`HEG-001`）は、サイズと SHA-256 が一致したものだけを置き、入手元と GPL-2.0 の `LICENSE` を添える。

## 配布時にやること（GPL-2.0 部分を含むバイナリを配る場合）
1. 同じ場所で対応するソース（この GitHub の該当タグ/コミット）を提供する。
2. `LICENSE`、`COPYING.md`、`LICENSES/`、各 `NOTICE` を一緒に入れる。
3. ファイル先頭の著作権表示・SPDX を消さない。
