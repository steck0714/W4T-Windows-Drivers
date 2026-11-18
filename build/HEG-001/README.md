# HEG-001 — Switch 有機EL（Mariko / Tegra X1+、コードネーム Aula）

**このリポジトリのソースからは、有機EL 向けをビルドしない。** 動作報告があるのは windows-switch v0.8.0 のビルド済み一式だけで、
報告は**配布者 1 人・1 台**（上流のリリースノート）。こちらでは再確認していない。

## 埋め方

```
python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip
```

zip は上流（DanilKhmyrov/windows-switch の Releases）から自分で取る。スクリプトは、`drivers/t214-aula/manifest/windows-switch-0.8.0.json` の
サイズと SHA-256 に**一致したファイルだけ**を `agent/` に書く（`.sys` 5 個、`swagentsvc.exe`）。違うものは書かずに報告する。
zip 内の名前は出力先に使わない（マニフェストの名前だけを使う）。

スクリプト・INF・ini などのテキストは、`drivers/t214-aula/kit/` にある。

## 守ること

- windows-switch の `swhid` / `swbatt` と、winx の `SWOA*` ドライバは**併用しない**（`docs/WINDOWS-SWITCH-COMPAT.md`）。
- 上流のライセンスは GPL-2.0（`LICENSE`）。配るときは上流の入手先と、このリポジトリの `CREDITS.md` を添える。
- Wi-Fi / BT の動作には、BCM4356 のファームと `BCM4356A3.hcd`、起動には `bl31.bin` と XUSB ファームが要る。
  いずれも入っていない。`fwfetch --profile aula --auto --deploy <ブート領域>` で自分の SD から取る。
