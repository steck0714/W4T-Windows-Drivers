# HDH-001 — Switch Lite（Mariko / Tegra X1+、コードネーム Hoag）

ソース: `drivers/t214-hoag/`（INF・テスト・ビルド手順つき）。**CI でのビルド設定がまだ無い**ので、`.sys` は置いていない。

## なぜ未作成か

- `drivers/t214-hoag/tools/Build-Driver.ps1` は、Erista 側（`drivers/t210-icosa/tools/Build-Driver.ps1`）より古く、
  `-NoWarnAsError`、`-CertThumbprint`、WDK 版の指定が無い。
- ドライバごとのビルド設定（ソース、ライブラリ、WDK 版）を書いた一覧（Erista の `drivers-t210.json` に当たるもの）が無い。
  推測で書くと、誤った設定が「ビルドできる」ように見えてしまうので書いていない。

## 手元でビルドするとき

`drivers/t214-hoag/README.md` と、その `docs/hdh001/` の手順（WDK、ARM64 ツールチェーン、テスト署名）に従う。
できた一式は `tools/build/assemble_build.py` と同じ形（`drivers/<キット名>/` + `MANIFEST.json` + `SHA256SUMS.txt`）で、ここへ置く。
Wi-Fi は WDK 10.0.26100 が必要。
