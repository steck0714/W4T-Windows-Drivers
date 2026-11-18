# HAC-001 — 初代 Switch（Erista / Tegra X1、コードネーム Icosa）

ソース: `drivers/t210-icosa/`。ビルド設定: `tools/ci/drivers-t210.json`（16 ドライバ）。
`drivers/` は、CI が作った一式を `tools/build/assemble_build.py` が置く場所。**いまは空**（`.sys` 未作成）。

## 入れる順番（キット名はビルド後のフォルダ名）

`Switch-Stage0`（読み込み試験）→ `Switch-Stage1`（GPIO）→ `Switch-Stage3`（I2C）→ Fan / CpuClock / Battery / PowerButton / Rtc /
Sensor / Touch / Bluetooth / Audio → `Switch-Sd`（**ブートスタート。最後に、INF 版数と testsigning を確認してから**）。
Wi-Fi は **1 つだけ**（`Switch-Wlan` か `Switch-Wifi-Probe`。同じ HWID `SWOA0D01`）。詳細は `docs/DRIVER-INVENTORY.md` の 5 章。

## 必要な外部ファイル（ここには入れない）

- GPU: NVIDIA の 9 ファイル → `fwfetch --profile erista --deploy-gpu <dir>`
- Wi-Fi / BT: Broadcom BCM4356 のファームと `BCM4356A3.hcd` → `fwfetch --profile erista`

## 状態

ビルド・実機とも未確認。`BUILD-INFO.md`（CI が書く）に OK / WARN / FAIL がドライバ別に出る。
