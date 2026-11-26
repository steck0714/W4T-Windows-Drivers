# fwfetch 1.2 の変更点（1.1 から）

目的: **Switchroot（L4T Ubuntu / Android）を一度 SD に入れていなくても**ファームウェアを取れるようにする。

## 追加
- `--image FILE`（`--disk` と同じだが、ファイル向け）。`l4t.00` を渡すと `l4t.01`, `l4t.02`, … を連結して 1 つの ext4 イメージとして読む（`src/image.rs`, `reader.rs`）。
- `<search-root>\switchroot\install\l4t.00` は自動検出（ファイル名は記憶に基づく・未確認）。
- `--archive FILE`: `.deb`(data.tar.gz/.xz/.bz2)・tar・tar.gz・tbz2・tar.xz から読む（`src/archive.rs`）。NVIDIA の `jetson-210_linux_r32.7.6_aarch64.tbz2` は中の `nv_tegra/nvidia_drivers.tbz2` まで自動で入る。zstd は未対応と表示するだけ。
- マニフェストのソースに `sha256` を追加。`gpu_nv_acr_ucode_prod` は L4T の `gm20b/acr_ucode.bin` / `tegra21x/acr_ucode.bin` を**文書化されたハッシュに一致した場合だけ**別名として採用し、検証済みとして記録する。

- `--download`: 足りないファイルだけを公式配布元から **Switch 本体で起動時に** 取得（`src/download.rs`、`curl.exe` をストリーム、`downloads` はマニフェスト）。NVIDIA の BSP（GPU / XUSB）と hekate 最新リリース（sc7）。zip の読み取りを追加（`archive.rs`、`crc32fast`）。
- 起動タスク: `FwFetchStartup.ps1` が既定で `--download` を付ける（`-NoDownload`、`-DownloadIntervalHours`＝6、タスクの実行時間上限 90 分）。
- 対象外: Wi-Fi / BT ファームウェアと bl31（公式の入手先が無い）。Wi-Fi は初回のみ USB LAN / テザリングが要る。

## 安全性
- 読み取りのみ（`File::open`）。アーカイブは展開せずメモリ上で必要なメンバーだけ保持。メンバー名は照合にのみ使い、出力パスには使わない。
- 展開サイズに上限（解凍爆弾対策）、ネスト深さ制限。`unsafe` は使っていない。

## 依存の追加
`lzma-rs 0.3`（MIT）、`bzip2 0.6`（MIT OR Apache-2.0）+ `libbz2-rs-sys 0.2`（bzip2-1.0.6、純 Rust）。`bzip2-rs 0.1.2` は複数ブロックのデータで失敗するため不採用。

## テスト（合成データのみ）
`tests/run_tests.py` に分割イメージ・.deb(xz/gz)・tbz2 入れ子・tar(GNU/pax 長い名前)・ハッシュ不一致・zstd/非アーカイブ・破損アーカイブ 160 件を追加。全 82 件合格（ローカル HTTP サーバーでの `--download` テスト 12 件を含む）、`cargo clippy -D warnings` 合格。

## 未確認
`--download` は実際の NVIDIA / GitHub に対して未検証（作業環境から到達できない）。
実際の Switchroot 配布物・NVIDIA BSP・実機では未検証。BSP 内のパス（`lib/firmware/tegra21x/…`）と ACR の別名対応は資料に基づく。bl31.bin・hekate の sc7 ファイルは BSP に無いので、従来どおり Switchroot 配布物/hekate を `--search-root` で渡す。
