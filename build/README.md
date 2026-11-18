# build/ — Switch の型番別ビルド済みドライバ置き場

`build/<型番>/` に、その型番向けの**完成したドライバ一式**を置く。ビルド済みバイナリ（`.sys` / `.cat` / `.exe`）はここに置いてよい
（作者の許可）。**再配布できないファームウェア・鍵は引き続き置かない**（`COPYING.md`、`tools/fwfetch/`）。

> **いまのこのフォルダは「置き場と手順」だけで、`.sys` は入っていない。**
> ドライバのビルドには Windows の WDK が要る。このフォルダを作った環境には WDK も Windows もなく、
> Microsoft 配布元にも届かなかったため、`.sys` を作れなかった。下の「埋め方」を実行すると、ここに入る。

| 型番 | 機種 | コードネーム | 状態 | 埋め方 |
|---|---|---|---|---|
| `HAC-001` | 初代 Switch（Erista） | Icosa | **CI でビルドできる**（未実行） | GitHub Actions `build-all-drivers`（手動実行で `commit_build` を ON）→ `build/HAC-001/drivers/` |
| `HAC-001-01` | 2019 年版（Mariko） | Iowa | **ドライバ一式が無い** | なし。Erista / Lite の流用はしない（基板・電源・I2C の一致が未証明） |
| `HDH-001` | Switch Lite | Hoag | ソースあり、**CI 設定が未作成** | `drivers/t214-hoag/` を手元の WDK でビルド。CI 化は未着手 |
| `HEG-001` | 有機EL | Aula | **上流のビルド済み（windows-switch v0.8.0）のみ** | `python tools/build/fill_heg001.py --zip windows-switch-arm64-drivers-v0_8_0.zip` |

機械可読な対応表は `models.json`。

## 各フォルダの中身（埋まったあと）

```
build/HAC-001/
  drivers/<キット名>/*.sys *.inf *.cat   # 1 つの証明書でテスト署名したもの
  SwitchWOA-Test.cer                      # 公開証明書だけ（秘密鍵は絶対に置かない）
  MANIFEST.json  SHA256SUMS.txt           # 全ファイルの SHA-256、INF の DriverVer と HWID
  BUILD-INFO.md                           # コミット、WDK 版、ドライバ別の OK / WARN / FAIL
  LICENSE COPYING.md CREDITS.md           # GPL-2.0 部分を含むので一緒に配る
```

## 守ること

- **テスト署名のみ**。`.cer` を信頼済みルートと信頼された発行元に入れ、`testsigning on` が要る。実機での動作確認は、どの型番も済んでいない。
- GPL-2.0 の部分（`TegraGpu` の電源実装など）を含む `.sys` は、**対応するソース（このリポジトリの該当コミット）と一緒に配る**。
- NVIDIA の GPU ファーム、Broadcom の Wi-Fi/BT ファーム、`bl31.bin` は入れない。自分の SD から `tools/fwfetch` で取り出す。
- `tools/check_no_proprietary.py` が、`build/` に秘密鍵とファームが紛れていないかも検査する。
