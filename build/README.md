# build/ — Switch の型番別ビルド済みドライバ置き場

`build/<型番>/` に、その型番向けの**完成したドライバ一式**を置く。ビルド済みバイナリ（`.sys` / `.cat` / `.exe`）はここに置いてよい
（作者の許可）。**再配布できないファームウェア・鍵は引き続き置かない**（`COPYING.md`、`tools/fwfetch/`）。

> **完成したドライバ一式は、CI が自動でここに置く**(2026-11-20 から)。`build-all-drivers` が成功するたびに、そのブランチの
> `build/<型番>/` へ `.sys` / `.inf` / `.cat` / 公開 `.cer` / `SHA256SUMS.txt` などをコミットする(`commit-build` ジョブ)。
> 取り込むには `git pull` するだけ。コミットさせずに取りたいとき・fork などは `python tools/build/sync_build.py`。
> 手元の Windows で `tools\ci\Build-AllDrivers.ps1 -Model <型番>` を実行した場合も、終わりに自動でここへ置かれる。
> **実機での動作は、どの型番も未確認。**

| 型番 | 機種 | コードネーム | 状態 | 埋め方 |
|---|---|---|---|---|
| `HAC-001` | 初代 Switch（Erista） | Icosa | **CI でビルド成功**（2026-11-19、#21）。実機未確認 | GitHub Actions `build-all-drivers`（push で自動）→ `build/HAC-001/drivers/` |
| `HAC-001-01` | 2019 年版（Mariko） | Iowa | **ドライバ一式が無い** | なし。Erista / Lite の流用はしない（基板・電源・I2C の一致が未証明） |
| `HDH-001` | Switch Lite | Hoag | **CI でビルド成功**（2026-11-19、#21。実機未確認）。設定は `tools/ci/drivers-t214-hoag.json`（16 ドライバ、うち 4 つは任意） | `build-all-drivers`（`models` に `HDH-001`）→ `build/HDH-001/drivers/` |
| `HEG-001` | 有機EL | Aula | **既定は CI でソースからビルド**（2026-11-19、#21 で成功。実機未確認。v0.8.0 とバイト一致はしない）。上流のビルド済み（windows-switch v0.8.0）を使うこともできる | 既定: `build-all-drivers`（`models` に `HEG-001`）→ `build/HEG-001/drivers/agent/`。上流版: `heg001_upstream` を ON、または手元で `python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip` |

機械可読な対応表は `models.json`。

## Actions で型番ごとに作る

`build-all-drivers`（Actions → Run workflow）の `models` に型番（`HAC-001,HDH-001` のようにカンマ区切り、または `all`）を入れる。型番ごとに別のジョブが走り、
`build-<型番>` というアーティファクトができる。**`main` / `master` / `test` への push でも自動で走り、成功した型番の一式が `build/<型番>/` に自動でコミットされる**
（`commit-build`。失敗した型番のフォルダは前のまま。push の自動コミットにアプリ（SwitchCenter / SwitchPatcher、大きい）は含めない。手動実行は `commit_build`（既定 ON）でアプリも置く）。
`HAC-001-01` は一式が無いので「スキップ」と表示される。コミットは `GITHUB_TOKEN` で行うので、次の実行は始まらない。
署名用の証明書を固定したいときはシークレット `SWOA_PFX_BASE64` / `SWOA_PFX_PASSWORD` を入れる（無いと実行ごとに別の自己署名になり、毎回 `.sys` と `.cer` が変わるので、そのたびにコミットが増える）。

自動でコミットさせずに自分の clone へ取りたいときは `python tools/build/sync_build.py --branch test`（`gh auth login` が要る。`--apps` でアプリも、`--artifacts-dir` で手でダウンロードしたものから）。

`release` と `heg001` は、`test` への push だけではスキップされる（仕様）。pre-release は、`v*` タグを push するか、手動実行で `release_tag`（例: `v0.0.1-test`）を入れると作られる。
Lite の GPU だけなら `build-lite-gpu` を手動実行して `release_tag` を入れる（`build-all-drivers` から呼ばれたときは動かない）。同名のタグがあると失敗する。

## 再配布できないファームは外から自動で読み込む

NVIDIA / Broadcom / Switchroot のファームは、**自分で用意した非公開の zip**（SD の `switchroot/…` や `lib/firmware/…` をコピーした形。GPU の 9 ファイルだけを zip 直下に置いた形も可）を
URL に置き、リポジトリのシークレット `FW_BUNDLE_URL` に入れる（任意で `FW_BUNDLE_SHA256`、`FW_BUNDLE_AUTH_HEADER`。実行時の入力 `firmware_url` でも可）。
**各ジョブの最初に自動で**取り込まれ（`.github/actions/fwfetch-import`）、fwfetch が照合・配置する。必要とするビルド（Erista の GPU プローブ）はそれを使い、無ければその部分だけ飛ばす。
ファームはランナーの一時領域にだけ置かれ、アーティファクトにもコミットにも入らない（`check_no_proprietary.py` が毎回確認）。

実機では、各 `build/<型番>/fwfetch/` の `Install-FwFetchStartup.ps1` を管理者 PowerShell で 1 回実行すると、**Windows の起動のたびに** fwfetch が自分の SD から不足分を読み込む
（全部見つかったら止まる。ログは `%ProgramData%\W4T\logs\fwfetch-startup.log`。Switch の Windows は C: とは限らないので、`-GpuDir auto -DriverDir auto` を使う）。

## 各フォルダの中身（埋まったあと）

```
build/HAC-001/
  drivers/<キット名>/*.sys *.inf *.cat   # 1 つの証明書でテスト署名したもの
  drivers/TegraGpu/<キット名>/*.sys *.inf *.cat *.dll  # GPU（probe / render / SwitchFan ブリッジ）。GPU ジョブの出力。
                                          # TegraGpuCore.lib と SHA256SUMS.txt も同じ場所。GPU ジョブが失敗した回は前回分が残る
  SwitchWOA-Test.cer                      # 公開証明書だけ（秘密鍵は絶対に置かない）
  MANIFEST.json  SHA256SUMS.txt           # 全ファイルの SHA-256、INF の DriverVer と HWID
  BUILD-INFO.md                           # コミット、WDK 版、ドライバ別の OK / WARN / FAIL
  LICENSE COPYING.md CREDITS.md           # GPL-2.0 部分を含むので一緒に配る
```

## 守ること

- **テスト署名のみ**。`.cer` を信頼済みルートと信頼された発行元に入れ、`testsigning on` が要る。実機での動作確認は、どの型番も済んでいない。
- GPL-2.0 の部分（`TegraGpu` の電源実装など）を含む `.sys` は、**対応するソース（このリポジトリの該当コミット）と一緒に配る**。
- NVIDIA の GPU ファーム、Broadcom の Wi-Fi/BT ファーム、`bl31.bin` は入れない。自分の SD から `tools/fwfetch` で取り出す。
- `tools/check_no_proprietary.py` が、`build/` に秘密鍵とファームが紛れていないかも検査する。

## 出来上がりの検査と、アプリ・Lite の GPU（2026-11-19 追加）

`build-all-drivers` は型番ごとに次も行う。**どれもビルドが通る・形式が正しいことの検査で、実機で動く証明ではない。**

- `tools/ci/verify_build.py`: 全 `.sys` が有効な PE で **ARM64・ネイティブ・署名済み**か、INF に `DriverVer` / `NTarm64` / 対応する `.cat` / `[SourceDisksFiles]` のファイルがそろっているか。1 件でも外れるとジョブが失敗する。
- InfVerif（参考表示。失敗しても止めない）。
- `apps` ジョブ: SwitchCenter / SwitchPatcher を win-arm64 で発行し、`.exe` が ARM64 か、鍵ファイルが無いかを検査 → `apps-<型番>` アーティファクト（`commit_build` で `build/<型番>/apps/`）。
- `gpu-lite` ジョブ（`build-lite-gpu.yml`）: HDH-001 の TegraGpu。ファームを取り込めた場合は probe と SwitchFan ブリッジを署名・カタログ化し、取り込めない場合は `TegraGpuCore.lib` だけ。表示ドライバや Vulkan ではなく、動作確認用の probe。
- HEG-001（OLED）は既定でソースから CI ビルドする（`swhid` / `swbatt` / `swagentsvc` ほか、`tools/ci/drivers-t214-aula.json`）。`heg001_upstream` を ON にすると、上流のビルド済み（ハッシュ照合）に切り替わる。上流の UEFI 側（`upstream-src/`）のビルドは、Windows のイメージから取り出した ARM64 バイナリ（ntoskrnl 等）と EWDK 入りの Docker が要るため、CI では行っていない。

## テスト署名キット（2026-11-19 追加、全型番）

各 `build/<型番>/test-signing/` に、その実行の `SwitchWOA-Test.cer` と `Install-TestSigning.cmd`（Switch で管理者実行 → 証明書を信頼 + `testsigning on`）、
`Stage0-SwitchSetup.ps1`、`Set-SwitchTestSigning.ps1`（PC から SD の BCD を直す）が入る。HAC-001-01 はスクリプトのみ（`.cer` なし）。→ `tools/testsign/README-ja.md`