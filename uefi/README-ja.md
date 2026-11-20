# uefi/ — UEFI / EDK II 側の開発用ソース

日本語 / [English](README.md) / [简体中文](README-zh-CN.md) / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / [Deutsch](README-de.md) / [한국어](README-ko.md)

Windows ドライバ(`drivers/`)を **EDK II(UEFI)から起動したときに確実に動かす**ための、UEFI 側のソースと
開発用の道具。**BL33 / bl31 などのファームウェアイメージ、バイナリは含まない**(それらは winx 本体のビルド成果物)。
実機では未確認。EDK II(ARM64)でのビルドもまだしていない(下の「確認済み / 未確認」)。

## なぜ要るか

TegraGpu の `tg_gpu_power_up()`(`platform/t210_power.c` = Erista、`platform/t214_power.c` = Lite / OLED)は、
GPU が **リセット中かつクランプ済み**(CAR `RESET_X` bit24 と PMC `0x7000e6d4` bit0 がともに 1)でないと
`TG_GPU_POWER_BUSY` を返して起動を拒否する(他の誰かが使っている GPU を横取りしないため)。
OLED の先行実装(DanilKhmyrov/windows-switch の `...gpu-gputestonly.patch`)も同じ前提で、
「GPU はリセット+クランプのまま渡し、電源投入はドライバがやる」設計。したがって UEFI 側の仕事は
**電源を入れることではなく、その状態で Windows に渡すこと**。

## 構成

| パス | 中身 |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | EDK II 非依存の中核(レジスタ操作のみ)。ホストでそのままテストできる |
| `edk2/GpuHandoff/GpuHandoff.c` | EDK II 側(winx の `PlatformBootManagerLib` に入る)。Windows 起動直前の確認と `ExitBootServices` での再確認 |
| `edk2/GpuHandoffApp/` | UEFI Shell 用ツール `GpuHandoff.efi`(開発時に BL33 を作り直さず状態を見る・直す)。winx では `Drivers/GpuHandoffApp/` としてパッチに入っている |
| `patches/winx-gpu-handoff.patch` | steck0714/winx の `usually` に当たるパッチ(`GpuHandoff.c/.h` の配置、DSC フラグ、呼び出し 3 か所、ホストテスト、Shell ツール `Drivers/GpuHandoffApp/` と DSC への追加) |
| `tests/gpu_handoff/` | 偽レジスタモデルのテスト(冷えた状態 / 稼働中 / MC が詰まる / 別 SoC / バスエラー / 引数不正)と、EDK II 向け `.c` の構文チェック |
| `tests/shim/` | 構文チェック専用の最小ヘッダ(EDK II の代わりではない) |
| `tools/check_uefi_contract.py` | ドライバ側の電源バックエンドとレジスタ・ビット・HIDREV・BUSY 条件が一致するかの静的検査。patch と `edk2/` の複製が一致するかも見る |

## 使い方

```
tools/run_host_tests.sh                       # 全体(uefi/ の分を含む)
sh uefi/tests/gpu_handoff/run.sh              # uefi/ のテストだけ
python3 -I uefi/tools/check_uefi_contract.py  # ドライバとの整合だけ
```

### winx に入れる

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista(要 SWITCH_BOARD_ERISTA と Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED(要 SWITCH_BOARD_OLED と Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite(既存フラグ。LiteHomeBoot から呼ばれる)
```

呼び出し位置: Erista / OLED は `SwitchBootMenu.c` の「Windows」起動オプション、Lite は `LiteHomeBoot.c` の
`BootWindowsFromMenu()`。どれも `PlatformGpuHandoffBeforeBoot()`。

### 実機での見方(UEFI Shell)

```
fs0:\> GpuHandoff.efi        読み取りだけ。cold なら Windows のドライバが引き取れる
fs0:\> GpuHandoff.efi -fix   復元してから前後を表示
```
`GpuHandoff.efi` は上のどれかの GPU フラグを付けたときだけビルドされる(FDF には入れない。XusbInit.efi と同じく、ビルド出力から SD カードにコピーする)。フラグなしのビルドは何も変わらない。

## 挙動の約束(GpuHandoff.h)

- 電源・PMIC・PLL には触らない。
- すでに cold なら**何も書かない**(何度呼んでも同じ)。
- cold でないときだけ、`tg_gpu_power_down()` と同じ順(MC ドレイン → クランプ → リセット → クロック停止)で戻す。
  **MC がドレインしなければクランプ/リセットには触らず**報告だけ(まだバスマスターの GPU をクランプするほうが危険)。
- 別の SoC(HIDREV 不一致)・読み出しが全ビット 1 のときは書き込まない。
- どの失敗でもブートは止めない。

## 確認済み / 未確認

- 確認済み: ホストテスト、EDK II 向け `.c` の構文チェック(Erista / Lite / OLED の 3 設定)、
  ドライバ側とのレジスタ・HIDREV・BUSY 条件の一致、パッチが winx `usually` に当たること。
  整合チェックは、ドライバ側の値を壊すと実際に失敗することを確かめてある(Shell ツールのコピーや DSC 追加がパッチからずれた場合も失敗する)。
- 未確認: **EDK II(ARM64)でのビルド(`GpuHandoff.efi` の DSC 追加を含む)、実機での動作**。OLED の呼び出し位置は winx の `SwitchBootMenu.c` 経由を前提にしていて、
  DanilKhmyrov 版の構成では位置が違う可能性がある。
- 入れていないもの: 準備できたときだけ ACPI の GPU0 を見せる処理(Wi-Fi の `WifiPrepareBoot.c` 方式)、GPU レールの UEFI 側制御。
