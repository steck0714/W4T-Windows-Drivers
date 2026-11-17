# W4T-Windows-drivers

Nintendo Switch (Tegra X1) 上の Windows ARM64 で動いているドライバを集め、**WinX の ACPI(SWOA 系 ID)に合わせて**
まとめたリポジトリ。Erista(T210/Icosa)、Lite(T214/Hoag)、Mariko(Iowa/Aula)を扱う。

> **未ビルド・未実機。** この環境には WDK が無く、`.sys` は作っていない。確認したのはソース/INF/DSDT の突き合わせと、
> ホストで動く単体テストだけ。実機で動く証明ではない。

## 構成

| パス | 中身 |
|---|---|
| `drivers/t210-icosa/` | Erista 用 WinX ドライバのソース・INF・kit（winx `usually` からの複製） |
| `drivers/t214-hoag/` | Lite 用（Wi-Fi 共通コア、TegraGpu、TegraXusb を含む） |
| `acpi/Dsdt.winx-usually.asl` | INF の HWID と突き合わせる基準 DSDT |
| `reference/windows-switch/` | windows-switch の swbatt / swhid / touchread / tune.ps1（参考、GPL-2.0）。移植元 |
| `reference/switchdrivers-*` | SwitchDrivers の manifest・段階0スクリプト |
| `docs/DRIVER-INVENTORY.md` | **全ドライバ一覧**（HWID・版・minUefi・系統・注意点） |
| `docs/WINDOWS-SWITCH-COMPAT.md` | windows-switch との互換性、併用禁止の組み合わせ |
| `docs/driver-gap-audit-ja.md`, `docs/HAYATO-DRIVER-AUDIT-ja.md` | winx 側の不足点検と hayato/ruje 突き合わせ |
| `tools/check_hwid_matrix.py` | INF の HWID が DSDT に存在するか検査（t210/t214 全 INF: OK） |
| `tools/run_host_tests.sh` | ホストで動くテスト一式 |

## この版で足した変更

- **t210 SwitchPowerButton 1.0.0.2**: windows-switch の swhid にあった「電源ボタン長押しで Ctrl+Alt+Del」を
  SWOA0601 に移植。レジストリ `HoldSasMs` で有効化（既定は無効で、従来と同じ動作）。→ `drivers/t210-icosa/drivers/SwitchPowerButton/README.md`

- Erista GPU（TegraGpu / ACPI\SWOA0F02）を取り込み、GPCPLL のロック検出と待ち時間を修正。詳細は `docs/ERISTA-GPU-INTEGRATION-ja.md`（実機未検証）。

## 使い方

    tools/check_hwid_matrix.py        # INF と DSDT の ID を検査
    tools/run_host_tests.sh           # ホスト側テスト（4 件: ID 検査、battery、bluetooth、長押しロジック）

ビルドと署名の手順は各 `kits/` と `drivers/*/README.md`（WDK 10.0.19041 以降、Wi-Fi は 10.0.26100、テスト署名 + `testsigning on`）。

## ライセンス・出典

- winx 由来のソース: 各ディレクトリの `LICENSE`（`drivers/*/LICENSE`）に従う。出典は winx の `NOTICE.winx.md`（hayato41200、ruje0504-2、CTCaer 等）。
- `reference/windows-switch/`: DanilKhmyrov/windows-switch の `LICENSE`（GPL-2.0）をそのまま同梱。再配布条件に注意。
- Broadcom ファームウェア、NVIDIA XUSB ファームウェア、ビルド済み `.sys` は現時点では未完成の為、含めない。
