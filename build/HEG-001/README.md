# HEG-001 — Switch 有機EL（Mariko / Tegra X1+、コードネーム Aula）

**既定では、他の型番(HAC-001 / HDH-001)と同じ CI 方式でソースからビルドする**(`.github/workflows/build-all-drivers.yml`、Windows ランナー + WDK + clang-cl、
`tools/ci/drivers-t214-aula.json`)。出力は `drivers/agent/`(windows-switch v0.8.0 の `agent\` と同じ並び)。**このリポジトリを作った環境では WDK も Windows も実機も無く、
CI は未実行。v0.8.0 のバイナリとバイト一致もしない。** 動作報告があるのは windows-switch v0.8.0 のビルド済み一式だけで、報告は**配布者 1 人・1 台**(上流のリリースノート)。こちらでは再確認していない。

| 出力 | 元 | 備考 |
|---|---|---|
| `agent/arm64/swhid.sys`, `swbatt.sys` | `drivers/t214-aula/drivers/SwitchHid`, `SwitchBattery`(windows-switch) | WDM。INF なし(`w64.cmd` がレジストリで登録) |
| `agent/swagentsvc.exe` | `drivers/t214-aula/drivers/SwitchAgent` | clang-cl で ARM64、静的 CRT |
| `agent/bt/switchbt.sys`, `agent/audio/switchaudio.sys`, `agent/wifi/switchwifi.sys` | `drivers/t214-hoag/` の同系統ソース | HWID(SWOA0B01 / 0C01 / 0D01)と版(1.0.0.19 / 1.0.0.9 / 0.5.7.0)は `kit/` の INF と一致。カタログは Switch 上で `w64.cmd` が作る |
| `agent/w64.cmd`, `tune.ps1`, INF, `wifi_sign.ps1` ほか | `drivers/t214-aula/kit/agent/`(無改変) | |

実行: Actions → `build-all-drivers` → `models = HEG-001`。署名は `SWOA_PFX_BASE64` / `SWOA_PFX_PASSWORD`(無ければ実行ごとの自己署名で、その実行の `.cer` を
Switch の Trusted Root / Trusted Publishers に入れる。`swhid` / `swbatt` はテスト署名が前提)。ファームウェアは CI に入れない(下記 fwfetch)。

### 以前の方式(上流バイナリの照合)

```
python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip
```

ワークフローを `heg001_upstream = true` で実行しても同じ(上流のリリースを取り、`drivers/t214-aula/manifest/windows-switch-0.8.0.json` のサイズと SHA-256 に**一致した
ファイルだけ**を `agent/` に書く)。動作報告のある v0.8.0 と同じバイナリが欲しいときはこちら。スクリプト・INF・ini などのテキストは `drivers/t214-aula/kit/` にある。

### ファームウェア(fwfetch)

windows-switch の `w64.cmd` は Wi-Fi / Bluetooth のファームを**起動パーティションの `agent\wifi` `agent\bt`** から読む。`--deploy` でそこへ置く:

```
fwfetch.exe --profile aula --auto --deploy E:\
```

起動タスク(`Install-FwFetchStartup.ps1 -Profile aula`)は検証済みの控えを `%ProgramData%\W4T\fw-cache` に作るだけで、GPU 以外は配置しない。

## 守ること

- windows-switch の `swhid` / `swbatt` と、winx の `SWOA*` ドライバは**併用しない**（`docs/WINDOWS-SWITCH-COMPAT.md`）。
- 上流のライセンスは GPL-2.0（`LICENSE`）。配るときは上流の入手先と、このリポジトリの `CREDITS.md` を添える。
- Wi-Fi / BT の動作には、BCM4356 のファームと `BCM4356A3.hcd`、起動には `bl31.bin` と XUSB ファームが要る。
  いずれも入っていない。`fwfetch --profile aula --auto --deploy <ブート領域>` で自分の SD から取る。
