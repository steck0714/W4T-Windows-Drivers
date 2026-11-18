# upstream-src/ の出自

- 入手元: DanilKhmyrov/windows-switch の `main`(ユーザーが `windows-switch-main.zip` として提供。zip の SHA-256 `c303804ca7c65fabd5424e6e044263117b56489c8e8ff6ffc1e4a2be05f7be65`)。
- 置いたもの: 上記 zip から **110 ファイル**を、`upstream-src/` に 103 ファイル(無改変)と、`drivers/<ドライバ名>/` に 7 ファイル(`layout.json` 参照。`.c` 4 本はクレジットブロック付き、INF 3 本は無改変)(`LICENSE` は `drivers/t214-aula/LICENSE` と同一なので省略、上流の `.gitignore` も省略)。
- 置かなかったもの(バイナリ・画像): `desktop/swagentsvc.exe`(10752 B、SHA-256 `50a685aa…89734d9`。`manifest/windows-switch-0.8.0.json` の v0.8.0 のものと同一)、
  `desktop/icon_windows.bmp`、`docs/screenshots/*.jpg`(3)、`docs/images/*.jpg`(1)。
- 公開証明書 `drivers/test-cert/*.crt` は置いた(秘密鍵は zip に無い。`PRIVATE KEY` の文字列検索で 0 件)。

## v0.8.0 との関係(確認できたこと / できないこと)

- 確認: `desktop/` と `drivers/` にある `tune.ps1` `w64.cmd` `remove_win10.ps1` `ssh_setup.ps1` `wifi_clean.ps1` `wifi_sign.ps1` と、3 つの INF
  (`switchwifi` `switchbt` `switchaudio`)は、`kit/` の v0.8.0 のものと**バイト一致**。`swagentsvc.exe` も一致。つまりこの `main` は v0.8.0 とほぼ同じ時点。
- 未確認: `swhid.c` / `swbatt.c` から v0.8.0 の `swhid.sys` / `swbatt.sys` が再現できるか(ビルドしていない)。`main` のそれ以外のファイルが v0.8.0 と同一かどうか。
- 未実行: `shim/test_shim.py`(`unicorn` とクロスツールチェーンが要る)、`uefi/` `drivers/` のビルド(Docker)。

## ライセンス

上流は GPL-2.0(`LICENSE`)。`patches/` は NintendoSwitchPkg / edk2 への差分で、当てる先のファイルは BSD License と GPL-2.0 が混在する。
ファイルごとの確認は `LICENSE-AUDIT.md`、帰属表示は `NOTICE.md`。配るときは上流の入手先と対応するソースを添える。
