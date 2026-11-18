# fwfetch — 再配布できないファイルを「自分の実機」から取り出す（Rust 版）

NVIDIA(NVIDIA Software License)・Broadcom・Switchroot ビルドのファイルは、このリポジトリにも配布物にも入れない。
Switchroot 系のやり方に倣い、**利用者自身の SD カード(Switchroot を入れたもの)から、読み取り専用で探して照合し、リポジトリの外に置く**。

2026-11-18 に PowerShell + C# 版（`legacy/`）を **Rust** で作り直した。SD カードの中身は信用できない入力なので、
メモリ安全な言語で書き、`unsafe` を禁止している（`#![forbid(unsafe_code)]`）。実行ファイル 1 つで動き、マニフェストは埋め込み済み。

## 使い方

```
fwfetch --profile aula|lite|erista|all  [--auto] [--search-root DIR]... [--disk FILE|\\.\PhysicalDriveN]...
        [--out-dir DIR] [--deploy BOOT_ROOT] [--deploy-gpu DIR] [--require-known-hash] [--manifest FILE] [--quiet]
```

例（Windows、SD を挿した管理者ターミナル。`E:` は windows-switch を展開したブート領域）:

```
fwfetch.exe --profile aula --auto --deploy E:\
fwfetch.exe --profile erista --auto --deploy-gpu C:\Windows\System32\drivers\TegraGpu
```

- 探す場所: 各ドライブの `switchroot\*\`（bl31.bin、initramfs）、`--search-root`（rootfs のコピー、カーネルツリー）、
  ext4 パーティション（`--disk`、または `--auto` + 管理者で全物理ディスク。**読み取りのみ**）。
- 見つけた候補は**公開されているサイズ/SHA-256 と照合**し、違えば捨てる。公開ハッシュが無いもの（bl31、NVIDIA GPU 一式）は初回採用で
  `UNVERIFIED` と表示し、`fw-lock.json` に固定する。
- 出力は `--out-dir`（既定 `%LOCALAPPDATA%\W4T\fw-cache`）。終了コード 0 = 全部見つかった、2 = 足りないものがある、1 = エラー。
- Wi-Fi の board ファイルは `macaddr=` を、ローカル管理のランダム MAC に置換（再実行しても同じ値）。

実行ファイルは `.github/workflows/build-fwfetch.yml` が作る（x86_64 と ARM64 の Windows 用）。手元なら `cargo build --release`。

## 安全のために入れていること

| 項目 | 内容 |
|---|---|
| 読み取り専用 | ソース（SD・ディスク・イメージ）は `File::open` だけで開く。書き込み用に開く経路が無い |
| `unsafe` 禁止 | 依存は `sha2` `flate2`（純 Rust の miniz_oxide）`serde_json` `getrandom` のみ（`THIRD-PARTY.md`） |
| 壊れた入力 | ext4 / GPT / cpio / gzip / Intel HEX の全オフセットを範囲・桁あふれ検査。リリース版も `overflow-checks` 有効。破損・切り詰めイメージ 210 件で落ちないことを確認 |
| 伸長爆弾 | gzip の展開量は 1 GiB で打ち切り。ファイルは 64 MiB、initramfs メンバーは 16 MiB まで |
| 出力先の制限 | 出力パスはマニフェストの相対パスだけから作る（`..`・絶対パス・`:`・`\` は読み込み時に拒否）。cpio 内の名前は照合にだけ使い、パスに使わない |
| リポジトリに置かない | `--out-dir`・`--deploy`・`--deploy-gpu` が git 作業ツリー内（シンボリックリンク経由も）なら拒否 |
| 書き込み | 一時ファイル → リネーム。出力先がシンボリックリンク・ディレクトリなら書かない |
| 配置前の再検証 | `--deploy` の前に、ステージ済みファイルの SHA-256 を `fw-lock.json` と照合。食い違えば配置しない |
| ネットワーク | 使わない（ダウンロードも通信も無い） |

## 任天堂の著作物について

このツールは Horizon のシステムファームも鍵（`prod.keys` など）もゲームデータも読まない。いまのドライバ構成にそれが必要だという根拠は
見つからなかった（`firmware-manifest.json` の `not_needed`）。「Horizon から Wi-Fi/BT ファームを抜く」経路は、二次情報だけで裏取りできていない
ため**実装していない**。

## テスト

`python3 tools/fwfetch/tests/run_tests.py`（cargo と mke2fs が必要）。合成データだけで、MBR/GPT、`/lib → usr/lib` シンボリックリンク、
複数ブロックのディレクトリ、ハッシュ検索、uImage+gzip+cpio、Intel HEX、MAC 置換、再実行、git 拒否、不正マニフェスト、シンボリックリンクへの
書き込み拒否、破損イメージ 150 件・破損 initramfs 60 件を確認する（30 件）。Linux で通過。

## 限界（正直に）

- **Windows ではビルドも実行もしていない。** コードは Linux でだけテストした。Windows 固有の部分（`\\.\PhysicalDriveN` を開く、ドライブレター走査）は
  どのテストも通っていない。
- **実機・実イメージで動かしていない**。パス（`lib/firmware/brcm/...`、`tegra21x/` など）の `layout: guess` は推測。ハッシュがあるものは
  誤りを弾けるが、GPU 一式と board ファイルは実イメージで要確認。
- Switchroot の initramfs が gzip/cpio でなければ取れない（zstd/lz4/xz は検出して「未対応」と表示するだけ）。
- ext4 は extents のみ・inline_data/暗号化なしを想定。古いブロックマップ方式は未対応。
- 物理ディスクの列挙は `PhysicalDrive0`〜`31` を順に開く方式（管理者権限が必要）。
- Lite の SD ミニポートは WINARM/FAT32 だけを公開する設計（`drivers/t214-hoag/README.md`）。実機の Windows から ext4 が見えない場合、
  Wi-Fi/BT/GPU 側は取れない（FAT32 側の bl31/XUSB は可）。その場合は PC にカードを挿して実行する。OLED で同じかは未確認。
- 走査は、最初に採用できたファイルで止まる（採用後に見つけた別候補の「未対応圧縮」警告は出ない）。

## 旧版

`legacy/` の `Get-SwitchFirmware.ps1` と `FwFetch.cs` は旧版（PowerShell 5.1 / 7 用）。もう更新しない。不要なら削除してよい。
