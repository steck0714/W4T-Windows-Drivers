# test-signing — テスト署名キット（全型番共通）

`build/<型番>/test-signing/` に入る。**HAC-001 / HAC-001-01 / HDH-001 / HEG-001 のどれでも同じ**手順。

| ファイル | 用途 |
|---|---|
| `SwitchWOA-Test.cer` | そのビルドの署名に使った公開証明書（秘密鍵ではない）。`.sys` / `.cat` と**同じ実行で作ったもの**だけが一致する |
| `Install-TestSigning.cmd` | Switch の Windows で管理者実行。`.cer` を Root と TrustedPublisher に入れ、`testsigning on`。再起動が要る |
| `Stage0-SwitchSetup.ps1` | 本体。`-Mode Prepare` / `Status`（BCD がシステムストアでない場合の修復つき） |
| `Set-SwitchTestSigning.ps1` | PC に SD を挿して、SD の BCD に直接 `testsigning` を入れる（Switch 側で `bcdedit` が開けないとき） |

## 注意
- 署名用 PFX を GitHub のシークレット（`SWOA_PFX_BASE64` / `SWOA_PFX_PASSWORD`）に入れると毎回同じ証明書になり、`.cer` を入れ直さずに済む。入れない場合は**実行ごとに別の証明書**なので、必ずその実行の `.cer` を使う。
- HAC-001-01（Iowa）はドライバ一式が無いので `.cer` は入らない（スクリプトだけ）。
- 実機での確認は済んでいない。

## 更新（アップデート）
新しいビルドを入れるときは、管理者の PowerShell で `Update-SwitchDrivers.ps1` を実行する（証明書の手作業は不要）。
- そのビルドの `SwitchWOA-Test.cer` が未信頼なら自動で信頼（Root と TrustedPublisher）に追加する。固定の証明書（シークレット `SWOA_PFX_BASE64`、作り方は `make-signing-cert.yml`）で作ったビルドなら、毎回同じ `.cer` なので何も起きない。
- 全 INF を `pnputil /add-driver … /install` で入れる。Windows は DriverVer が高いときだけ置き換えるので、CI は毎回 DriverVer を増やして入れる（`tools/ci/stamp_driver_ver.py`）。
- `-CheckOnly` で入れずに一覧だけ見られる。ログ: `%ProgramData%\W4T\logs\update-drivers.log`。再起動で新しいドライバが読み込まれる。
- ファームウェアは更新に含まれない（fwfetch の起動時タスクが保つ）。
- テスト署名自体が未設定なら先に `Install-TestSigning.cmd` を一度実行して再起動する。
