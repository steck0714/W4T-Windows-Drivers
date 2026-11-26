# 同梱される Rust ライブラリ（fwfetch の実行ファイルにリンクされるもの）

`Cargo.lock` の実行時依存（全ターゲットの合計。Windows 向けにリンクされるのはこのうち一部）。
いずれも **MIT で使う**（`MIT OR Apache-2.0` の MIT 側を選ぶ。Apache-2.0 は GPL-2.0-only と両立しないため選ばない）。
ビルド時だけ動くもの（proc-macro の serde_derive / syn / quote / proc-macro2 / unicode-ident、generic-array の build.rs が使う version_check）は実行ファイルに入らないが、`Cargo.lock` に載るので参考として一覧に含める。

| クレート | 版 | ライセンス表記 |
|---|---|---|
| adler2 | 2.0.1 | 0BSD OR MIT OR Apache-2.0 |
| block-buffer | 0.10.4 | MIT OR Apache-2.0 |
| bzip2 | 0.6.1 | MIT OR Apache-2.0 |
| cfg-if | 1.0.5 | MIT OR Apache-2.0 |
| cpufeatures | 0.2.17 | MIT OR Apache-2.0 |
| crc32fast | 1.5.5 | MIT OR Apache-2.0 |
| crypto-common | 0.1.7 | MIT OR Apache-2.0 |
| digest | 0.10.7 | MIT OR Apache-2.0 |
| flate2 | 1.1.11 | MIT OR Apache-2.0 |
| generic-array | 0.14.7 | MIT |
| getrandom | 0.3.4 | MIT OR Apache-2.0 |
| itoa | 1.0.19 | MIT OR Apache-2.0 |
| libbz2-rs-sys | 0.2.7 | bzip2-1.0.6 |
| libc | 0.2.191 | MIT OR Apache-2.0 |
| lzma-rs | 0.3.0 | MIT |
| memchr | 2.8.3 | Unlicense OR MIT |
| miniz_oxide | 0.9.1 | MIT OR Zlib OR Apache-2.0 |
| proc-macro2 | 1.0.107 | MIT OR Apache-2.0 |
| quote | 1.0.47 | MIT OR Apache-2.0 |
| r-efi | 5.3.0 | MIT OR Apache-2.0 OR LGPL-2.1-or-later |
| serde | 1.0.229 | MIT OR Apache-2.0 |
| serde_core | 1.0.229 | MIT OR Apache-2.0 |
| serde_derive | 1.0.229 | MIT OR Apache-2.0 |
| serde_json | 1.0.154 | MIT OR Apache-2.0 |
| sha2 | 0.10.9 | MIT OR Apache-2.0 |
| simd-adler32 | 0.3.10 | MIT |
| syn | 3.0.10 | MIT OR Apache-2.0 |
| typenum | 1.20.1 | MIT OR Apache-2.0 |
| unicode-ident | 1.0.27 | (MIT OR Apache-2.0) AND Unicode-3.0 |
| version_check | 0.9.5 | MIT OR Apache-2.0 |
| wasip2 | 1.0.1+wasi-0.2.4 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT |
| wit-bindgen | 0.46.0 | Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT |
| zlib-rs | 0.6.8 | Zlib |
| zmij | 1.0.23 | MIT |

配布するときは、各クレートの著作権表示と許諾文（crates.io の各ページ、または `cargo vendor` した各ソースの LICENSE-MIT）を実行ファイルと一緒に提供すること。
