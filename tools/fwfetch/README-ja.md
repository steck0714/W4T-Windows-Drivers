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
| ネットワーク | 使わない（ダウンロードも通信も無い。CI の取得は `tools/ci/fw_bundle.py` が別に行う） |

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

## 起動時に自動で読み込む / CI で外から読み込む

**実機（Windows 起動時）**: `startup/Install-FwFetchStartup.ps1` を、`fwfetch.exe`（ARM64 版）と同じフォルダで、管理者 PowerShell から 1 回実行する。

```
.\Install-FwFetchStartup.ps1 -Profile lite -GpuDir C:\Windows\System32\drivers\TegraGpu
.\Install-FwFetchStartup.ps1 -Remove
```

SYSTEM で動くスケジュールタスクが、Windows の起動 1 分後に `fwfetch --profile <p> --auto --out-dir C:\ProgramData\W4T\fw-cache` を実行する。
全部見つかる（終了コード 0）と印を付けて以後は何もしない。足りない（2）・エラー（1）なら次の起動でまた試す。SD には書かない。通信もしない。
ログ: `C:\ProgramData\W4T\logs\fwfetch-startup.log`。

**CI（Actions）**: `.github/actions/fwfetch-import` が、非公開 zip（シークレット `FW_BUNDLE_URL`）をジョブの最初に自動で取得・展開し（`tools/ci/fw_bundle.py`。
`..`・絶対パス・シンボリックリンク・巨大展開は拒否）、`fwfetch --search-root` で照合する。ダウンロードはこの action 側で行い、fwfetch は今までどおりオフライン。
ファームはランナー上にだけ残り、アーティファクトにもコミットにもならない。

## 旧版

`legacy/` の `Get-SwitchFirmware.ps1` と `FwFetch.cs` は旧版（PowerShell 5.1 / 7 用）。もう更新しない。不要なら削除してよい。

### 壊れたら自動で入れ直す（2026-11-19 追加）

`FwFetchStartup.ps1` は起動のたび（と毎日 03:30）、まず SD なしで次を検査する。

1. キャッシュ（`C:\ProgramData\W4T\fw-cache`）の各ファイルの SHA-256 を `fw-lock.json` と照合
2. `-GpuDir` を渡していれば、ドライバフォルダの GPU ファイル 9 個も照合

結果に応じて、

- 配置先だけ壊れた/消えた → キャッシュの正しいコピー（ロックと再照合）から書き戻す。SD は不要
- キャッシュが壊れた/消えた、`fw-lock.json` が読めない、完了印が無い → fwfetch を再実行し、SD から取り直して再配置・再固定
- 最後にもう一度検査し、全部そろったときだけ完了印（`done.<profile>`）を書く。残りがあれば終了コード 2 で、次回また試す

`fw-lock.json` はパスを検査してから使う（`..`・絶対パス・`:`・`\` を含むものは壊れた項目として扱う）。**このスクリプトは Windows で未実行**（PowerShell が無い環境で書いたため構文確認もしていない）。
