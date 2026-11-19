# 「全部フルでビルドできるか」の確認 — 2026-11-19

**答え: この環境では確認できない。** WDK も Windows ランナーも無く、CI は未実行。ここで確かめたのは次の静的な範囲だけ。

## 確認できたこと
- `drivers-t210.json`（16）/ `drivers-t214-hoag.json`（16）/ `drivers-t214-aula.json`（5）の**全エントリのソースと INF が実在する**。
- `build-all-drivers.yml` は YAML として正しい。`verify_build.py` と `build/` 組み立てスクリプトのテストは通る（新規のテスト署名キットのテスト 7 件を含む）。
- `TegraGpuCore.lib`（ARM64）が CI で実際に作れている（Erista-GPU.zip）。

## まだ分からない／落ちそうな所
- 各ドライバの clang-cl コンパイル（`/W4 /WX`）、WDK 10.0.19041 の導入（失敗時は 26100 に切り替わる）、Wi-Fi の WDK 26100 / WiFiCx、`swagentsvc.exe`、InfVerif。
- 任意（optional）扱いで落ちても全体は止まらないもの: swoahello(Lite)、swoadiag(Lite)、switchcpu(Lite)、switchwifi、switchwprobe、HEG-001 の switchbt / switchaudio / switchwifi。**緑でもこれらが欠けている可能性があるので、`BUILD-INFO.md` の OK/WARN/FAIL を見る。**
- GPU の probe は NVIDIA ファームが無いと作られない（`ERISTA-GPU-CHECK-ja.md`）。
- HAC-001-01（Iowa）はドライバ一式が無いので「フル」の対象外（テスト署名スクリプトのみ）。
- 動作確認が取れているのは windows-switch v0.8.0 の OLED 1 台の報告だけ。

## 今回直したもの
- Lite の `Switch-Stage0/SwitchWOA-Test.cer` はリポジトリに固定の古い証明書で、**実行ごとの自己署名だと署名と一致しない**まま配られていた。`Build-AllDrivers.ps1` が、その実行の `.cer` で上書きするようにした。
