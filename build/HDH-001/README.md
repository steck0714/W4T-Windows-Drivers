# HDH-001 — Switch Lite（Mariko / Tegra X1+、コードネーム Hoag）

ソース: `drivers/t214-hoag/`。ビルド設定: `tools/ci/drivers-t214-hoag.json`（16 ドライバ）。`build-all-drivers` を `models=HDH-001` で動かすと、
Windows ランナーでビルド・テスト署名され、`drivers/` に置かれる。**CI が自動で置く(まだ一度も走っていなければ空)**（CI ではビルドできている: 2026-11-19、`build-all-drivers` #21。`build-all-drivers` が成功するたびに自動で置かれる(push でも手動実行でも)）。実機は未確認。

## 設定の根拠

`drivers/t214-hoag/docs/hdh001/build-drivers.sh`（GPIO・I2C・タッチ・電源ボタン・ファン・電池・RTC・Bluetooth・sio・XUSB）と、各ドライバの README
（SD ミニポート、音声、Wi-Fi）から書き起こした。`SWITCH_LITE_TOUCH=1` は sh スクリプトと同じく該当ドライバに付けている。
`drivers/t214-hoag/tools/Build-Driver.ps1` は Erista 側の新しい版（`-NoWarnAsError`、`-CertThumbprint`、WDK 版の指定つき）に置き換えた。引数の既定値は旧版と同じ。
`kmdf_support.c` は Lite 自身のものを使う（Erista のものと内容が違う）。

## 任意（失敗してもジョブは落ちない）

`swoahello`、`swoadiag`（診断用）、`switchcpu`（Lite では読み込まない）、`switchwifi`（WiFiCx。WDK 10.0.26100 と `check-core.py` の手順を `Build-Driver.ps1` に移したもので、初回は特に未確認）。

## ここに入らないもの

- Windows 用 GPU ドライバ（Lite では未実装）
- SD ミニポートの WINARM 版（カードごとの非公開ヘッダが要る。`drivers/t214-hoag/Tegra210Sdhc/README.md`）
- SwitchCenter / SwitchPatcher（アプリ）

## 必要な外部ファイル（ここには入れない）

BCM4356 のファームと `BCM4356A3.hcd`、`bl31.bin`、XUSB ファーム。実機では `fwfetch/Install-FwFetchStartup.ps1 -Profile lite` で起動時に自動で読み込む。