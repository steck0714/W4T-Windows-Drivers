# fwfetch — 再配布できないファイルを「自分の実機」から取り出す（Rust 版）

NVIDIA(NVIDIA Software License)・Broadcom・Switchroot ビルドのファイルは、このリポジトリにも配布物にも入れない。
**利用者自身のファイルから、読み取り専用で探して照合し、リポジトリの外に置く**。
v1.2 から **Switchroot（L4T Ubuntu / Android）を一度 SD に入れていなくても使える**（下の「Switchroot を入れずに使う」）。

2026-11-18 に PowerShell + C# 版（`legacy/`）を **Rust** で作り直した。SD カードの中身は信用できない入力なので、
メモリ安全な言語で書き、`unsafe` を禁止している（`#![forbid(unsafe_code)]`）。実行ファイル 1 つで動き、マニフェストは埋め込み済み。

> **Switchroot(L4T Ubuntu / LineageOS)をこの Switch に入れないこと。** 共存させると Windows と Linux のどちらかが壊れて使えなくなることが確認されている。
> 取得元はダウンロードしただけのファイル(`bl31.bin`、`switchroot/install/l4t.00`・`l4t.01`…、NVIDIA の BSP)でよい。

## 使い方

```
fwfetch --profile aula|lite|erista|all  [--auto] [--search-root DIR]... [--disk FILE|\\.\PhysicalDriveN]...
        [--image FILE]... [--archive FILE]...
        [--out-dir DIR] [--deploy BOOT_ROOT] [--deploy-gpu DIR|auto] [--deploy-drivers DIR|auto] [--require-known-hash] [--manifest FILE] [--quiet]
```

例（Windows、SD を挿した管理者ターミナル。`E:` は windows-switch を展開したブート領域。**ドライブ文字は環境しだい**なので自分の文字に読み替える）:

```
fwfetch.exe --profile aula --auto --deploy E:\
fwfetch.exe --profile erista --auto --deploy-gpu auto
```

### Switchroot を入れずに使う（v1.2）

これまでは「Switchroot を入れた SD の ext4 パーティション」から読む前提だった。次の入力でも読めるようにした（どれも読み取り専用、展開もフラッシュもしない）。

| 入力 | 取れるもの | 備考 |
|---|---|---|
| `--image l4t.00` | rootfs 全体（GPU / Wi-Fi / BT / XUSB の firmware） | Switchroot の配布物にある分割 rootfs イメージ。`l4t.00`→`l4t.01`→… を連結して 1 つのイメージとして読む。SD に Flash Linux する必要なし。`<root>\switchroot\install\l4t.00` は `--search-root`/`--auto` でも自動で見つかる |
| `--archive jetson-210_linux_r32.7.6_aarch64.tbz2` | GPU firmware・XUSB firmware | NVIDIA 公式の Jetson Linux(L4T) ドライバパッケージ。中の `nv_tegra/nvidia_drivers.tbz2` まで自動で入る |
| `--archive nvidia-l4t-firmware_*.deb` など | 同上 | `.deb`（data.tar.gz / .xz）、tar、tar.gz、tar.xz、tbz2 |

- **zstd は未対応**（検出して「未対応」と表示するだけ。推測しない）。zstd の .deb は `dpkg-deb -x` などで展開してから `--search-root` で渡す。
- 取れないまま残るもの: **bl31.bin**（`switchroot\ubuntu\` などに置かれる Switchroot 側のファイル）、hekate の sc7entry/sc7exit（hekate のフォルダに在る。`--search-root` で渡す）、
  Wi-Fi の board ファイル。これらは NVIDIA の BSP には含まれないので、Switchroot 配布物（7z を展開しただけでよい）か hekate を `--search-root` に渡す。
- **`nv_acr_ucode_prod.bin`**: L4T では `gm20b/acr_ucode.bin`（または `tegra21x/acr_ucode.bin`）という名前。Windows ドライバ側の名前との別名として、
  **mesa-switch-woa の `docs/DRIVER.md` に書かれている SHA-256 と完全に一致した場合だけ**採用する（`firmware-manifest.json` の `sha256`）。違うバイト列は別名では採用しない。
  この別名の対応づけは、手元の資料の記述に基づくもので、実イメージでは未確認。
- `--require-known-hash` でも、この別名（ハッシュ付きソース）は採用される。

### 起動時に Switch 本体で自動ダウンロード（v1.2 `--download`）

PC 側ではなく、**Switch の Windows 起動時（起動タスク）に本体の中で**足りないファイルを公式配布元から取る。

```
fwfetch.exe --profile aula --auto --download --deploy-gpu auto --deploy-drivers auto
```

- ローカル（SD・`--image`・`--archive`・`--search-root`）を先に探し、**それでも見つからない ID だけ**を、マニフェストの `downloads` から取る（見つかっていれば通信しない）。
  - GPU firmware 一式・XUSB: NVIDIA 公式 `jetson-210_linux_r32.7.6_aarch64.tbz2`（約 1 GB）。
  - sc7entry / sc7exit: hekate の最新リリース zip（GitHub API で最新を解決。資産名は `hekate_ctcaer_*.zip`、URL は `github.com/CTCaer/hekate/releases/download/` のみ）。
- ダウンロードは Windows 10 1803 以降に標準で入っている `curl.exe` を使い、**受信しながらアーカイブとして読む**（巨大な一時ファイルを SD に作らない。必要なメンバーだけメモリに保持）。TLS の自前実装は無い。
- 許可するのは `https` と `developer.nvidia.com` / `developer.download.nvidia.com` / `github.com` / `api.github.com` のみ（マニフェスト検証時に拒否。リダイレクトも https のみ）。
  ダウンロードしたデータの中身は、ローカルのファイルと同じく**マニフェストのハッシュで検証**（ハッシュの無いものは初回採用で `UNVERIFIED` + `fw-lock.json` に固定）。
- 起動タスクは既定で `--download` 付き（`Install-FwFetchStartup.ps1 -NoDownload` で無効）。失敗は警告にして次回に再試行するが、**既定で 6 時間以内には再ダウンロードしない**（`-DownloadIntervalHours`）。実行時間の上限は 90 分。
- **それでも自動では取れないもの**: Wi-Fi / Bluetooth のファームウェア（BCM4356A3）と bl31.bin は、公式の配布元が見つかっていないので `--download` の対象外（Switchroot / Linux 側のファイルが必要）。
  また Wi-Fi のファームウェア自体が無い間は Wi-Fi でダウンロードできない。**最初の 1 回は USB LAN / USB テザリング**が必要。hekate が起動時に読む sc7 ファイルは Windows 起動前に要るので、
  Windows から入れた分は**次回の起動から**効く。
- **未検証**: 実際の NVIDIA / GitHub への接続・実機（Windows on ARM の curl.exe）では試していない。ローカル HTTP サーバーでのテストのみ。NVIDIA の URL にはチェックサムの公開が無い。

### ドライブ文字について（2026-11-20 修正）

Switch の Windows は SD カード上にあるので、**システムドライブが C: とは限らない**（逆に C: のほうが珍しいこともある）。次の前提を外した。

- `--deploy-gpu auto` / `--deploy-drivers auto` は、**いま動いている Windows の `%SystemRoot%\System32\drivers[\TegraGpu]`** に置く（文字を打たない）。
  `%SystemRoot%` が無い環境（Linux など）では `auto` は**エラー**にする（`C:` に決め打ちして別のディスクへ書くことはしない）。
- パスには `%VAR%` が使える（例 `--deploy-gpu %SystemRoot%\System32\drivers\TegraGpu`）。**未定義の変数はエラー**（空文字にしない）。`--out-dir` / `--deploy` も同じ。`auto` は `--out-dir` / `--deploy` では使えない。
- `--auto` は、文字が付いた全ドライブ（A:〜Z:）に加えて、**ドライブ文字が付いていないボリューム**（Switchroot の FAT32 `switchroot` 領域は文字が無いことが多い）も、
  `mountvol.exe`（`%SystemRoot%\System32` のもの、引数なし）の一覧から `\\?\Volume{GUID}\` として探す。ログに `N volume(s) without a drive letter` と各 `search root` が出る。
  文字の無いブート領域へ `--deploy` したいときは、その `\\?\Volume{GUID}\` をそのまま渡せる。
- 同じ探索先（同じ文字、大文字小文字違い、末尾の `\` 違い）は 1 回だけ見る。
- 起動タスク（下記）は、ドライブ文字をタスクに書き込まない（`-GpuDir auto -DriverDir auto`）。文字が変わっても動き続ける。

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
| ネットワーク | 既定では使わない。`--download` を付けたときだけ、`curl.exe` で許可ホスト（NVIDIA / GitHub）へ https 接続する |

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
- **v1.2 の分割イメージ/アーカイブ対応は合成データのテストのみ**。実際の `l4t.00` や NVIDIA の BSP では未確認（ファイル名 `switchroot/install/l4t.00` は記憶に基づく。見つからなければ `--image` で直接指定する）。
- アーカイブは必要なメンバーだけをメモリに保持する。BSP 全体（約 1.5GB の tar）を一度読むので数十秒〜数分かかる。
- ext4 は extents のみ・inline_data/暗号化なしを想定。古いブロックマップ方式は未対応。
- 物理ディスクの列挙は `PhysicalDrive0`〜`31` を順に開く方式（管理者権限が必要）。
- Lite の SD ミニポートは WINARM/FAT32 だけを公開する設計（`drivers/t214-hoag/README.md`）。実機の Windows から ext4 が見えない場合、
  Wi-Fi/BT/GPU 側は取れない（FAT32 側の bl31/XUSB は可）。その場合は PC にカードを挿して実行する。OLED で同じかは未確認。
- 走査は、最初に採用できたファイルで止まる（採用後に見つけた別候補の「未対応圧縮」警告は出ない）。

## 起動時に自動で読み込む / CI で外から読み込む

**実機（Windows 起動時）**: `startup/Install-FwFetchStartup.ps1` を、`fwfetch.exe`（ARM64 版）と同じフォルダで、管理者 PowerShell から 1 回実行する。

```
.\Install-FwFetchStartup.ps1 -Profile lite -GpuDir auto -DriverDir auto
.\Install-FwFetchStartup.ps1 -Remove
```

`-GpuDir` / `-DriverDir` は `auto`（= いまの Windows の `System32\drivers\TegraGpu` / `System32\drivers`）を勧める。
自分で書いたパスが**いまの Windows の `System32\drivers` 配下なら `auto` として保存**する（後で文字が変わっても追従）。
別の文字の `…\System32\drivers` を書いた場合は、いまの Windows に読み替えて警告を出す。

SYSTEM で動くスケジュールタスクが、Windows の起動 1 分後に `fwfetch --profile <p> --auto --out-dir %ProgramData%\W4T\fw-cache` を実行する（`%ProgramData%` はシステムドライブ上。タスク自身も `$env:ProgramData` から実行時にスクリプトを探す）。
全部見つかる（終了コード 0）と印を付けて以後は何もしない。足りない（2）・エラー（1）なら次の起動でまた試す。SD には書かない。通信は `--download`（起動タスクの既定）で足りないファイルがあるときだけ（上の「起動時に Switch 本体で自動ダウンロード」）。
ログ: `%ProgramData%\W4T\logs\fwfetch-startup.log`（先頭付近に `Windows is on <SystemRoot>; GpuDir=… DriverDir=…` が出る）。

**CI（Actions）**: `.github/actions/fwfetch-import` が、非公開 zip（シークレット `FW_BUNDLE_URL`）をジョブの最初に自動で取得・展開し（`tools/ci/fw_bundle.py`。
`..`・絶対パス・シンボリックリンク・巨大展開は拒否）、`fwfetch --search-root` で照合する。ダウンロードはこの action 側で行い、fwfetch は今までどおりオフライン。
ファームはランナー上にだけ残り、アーティファクトにもコミットにもならない。

## 旧版

`legacy/` の `Get-SwitchFirmware.ps1` と `FwFetch.cs` は旧版（PowerShell 5.1 / 7 用）。もう更新しない。不要なら削除してよい。

### 壊れたら自動で入れ直す（2026-11-19 追加）

`FwFetchStartup.ps1` は起動のたび（と毎日 03:30）、まず SD なしで次を検査する。

1. キャッシュ（`%ProgramData%\W4T\fw-cache`）の各ファイルの SHA-256 を `fw-lock.json` と照合
2. `-GpuDir` を渡していれば、ドライバフォルダの GPU ファイル 9 個も照合

結果に応じて、

- 配置先だけ壊れた/消えた → キャッシュの正しいコピー（ロックと再照合）から書き戻す。SD は不要
- キャッシュが壊れた/消えた、`fw-lock.json` が読めない、完了印が無い → fwfetch を再実行し、SD から取り直して再配置・再固定
- 最後にもう一度検査し、全部そろったときだけ完了印（`done.<profile>`）を書く。残りがあれば終了コード 2 で、次回また試す

`fw-lock.json` はパスを検査してから使う（`..`・絶対パス・`:`・`\` を含むものは壊れた項目として扱う）。**このスクリプトは Windows で未実行**（PowerShell が無い環境で書いたため構文確認もしていない）。
