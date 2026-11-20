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
