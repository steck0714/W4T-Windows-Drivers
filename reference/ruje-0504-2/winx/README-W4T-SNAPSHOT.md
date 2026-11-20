# ruje0504-2/winx — 上流のままの参照コピー(ビルド済みバイナリを除く)

上流: https://github.com/ruje0504-2/winx  コミット `0d797d1f39f295dec93cf2fb935ede0751f01353`(取得 2026-11-20)。ライセンス: **GPL-2.0-only**(`LICENSE`、`NOTICE.winx.md`。EDK2 由来は BSD-2-Clause-Patent、ファイルの先頭に従う)。**無改変**。
W4T の方針(ビルド済みは `build/` だけ)に合わせて、**`.bin` / `.efi` / `.fsdrv` / `.img` は入れていない**(27 ファイル。パスと SHA-256 は `EXCLUDED-BINARIES.sha256`。上流から取得できる)。
`bl33.bin` を配るときの対応ソースの扱いは `NOTICE.winx.md` の 4 を参照。

- 上流の `README.md` などはそのまま。この説明は `README-W4T-SNAPSHOT.md`。
- 全ファイルの SHA-256: `MANIFEST.sha256`(この説明と `EXCLUDED-BINARIES.sha256`、自身を除く)。
