# NOTICE — 取り込んだ第三者のコード・データの帰属

`upstream-src/` と `drivers/` は上流(DanilKhmyrov/windows-switch `main`、GPL-2.0)。上流のファイルへの変更は、`drivers/` の 4 本の `.c` に足したクレジットブロックと `swhid.c` の `#include` 1 行だけ(`layout.json`、`tools/aula_layout.py --check` で SHA-256 検証)。
上流の表示では足りないところを、ここで補う。ファイルごとの根拠は `LICENSE-AUDIT.md`。

## Intel Corporation — edk2 `MdeModulePkg/Universal/Console/GraphicsConsoleDxe/LaffStd.c`

`upstream-src/patches/nintendoswitchpkg-zarm32.patch` が追加する `Library/Tegra8250SerialPortLib/FbFont.h` の 8x19 グリフは、
この `LaffStd.c`(edk2-stable202102)から取ったもの。元ファイルの表示:

    Copyright (c) 2006 - 2008, Intel Corporation. All rights reserved.<BR>
    SPDX-License-Identifier: BSD-2-Clause-Patent

パッチ内の `FbFont.h` にはこの著作権表示が無い(「LaffStd.c 由来(BSD-2-Clause-Patent)」の 1 行のみ)。BSD-2-Clause-Patent は
ソース再配布で著作権表示・許諾条件・免責の保持を求めるので、**上のとおり表示する**。条文は `LICENSES/BSD-2-Clause-Patent.txt`。
(上流には、パッチに Intel の表示を入れるよう報告するのが望ましい。)

## ARM Limited / Linaro / Red Hat / Andrei Warkentin / Bingxing Wang — NintendoSwitchPkg の BSD License ファイル

パッチは次のファイルを書き換える(表示は残っている)。パッチを当てたあとも、これらのファイル先頭の著作権表示と BSD License の文面を**消さない**こと:
`AcpiTables/Dsdt/Dsdt.asl`、`Library/MemoryInitPeiLib/MemoryInitPeiLib.c`、`Library/NintendoSwitchPlatformLib/NintendoSwitchPlatformLib.inf`、
`Library/NintendoSwitchPlatformLib/Switch.c`、`NintendoSwitch.dsc`。
`zarm32` が追加する `Library/NintendoSwitchPlatformLib/Arm/ArmPlatformHelper.S`(32 ビット版)は、同ディレクトリの
ARM Ltd の BSD ファイル `ArmPlatformHelper.S`(AArch64 版)の派生なので、同じ BSD の表示を引き継ぐ。
元: https://github.com/imbushuo/NintendoSwitchPkg (commit `09c3ade486aa76b53557d930ed291760cea41cb0`)、ライセンス表示は各ファイル。

## naehrwert — `Include/Foundation/Types.h`

GPL-2.0(version 2)。`Copyright (c) 2018 naehrwert`。パッチが 1 本触る。表示を消さない。

## ruje0504-2 / steck0714 / hayato41200 — Wi-Fi・Bluetooth・音声ドライバと INF

`upstream-src/drivers/switchwifi/` `switchbt/` `switchaudio/` の INF は、ruje0504-2/Windows-for-Switch-Drivers(GPL-2.0)の INF の文字列だけを変えたもの。
`patches/*wifi*` `*audio*` `*usb*` の一部は steck0714/winx と hayato41200/Windows-for-Switch(いずれも GPL-2.0)からの移植と書かれている。
`drivers/build_gpu.sh` と `drivers/nvk/tgd3d-local.patch` が扱う mesa-switch-woa の `tegra-x1-wddm/` は MIT と GPL-2.0 の混在で、**ファイルごとの SPDX を維持すること**。
出典の一覧は `CREDITS.md`。

## CTCaer(hekate)/ Switchroot / Linux / coreboot

パッチや shim のコメントが手順・レジスタ値の出典として挙げている(hekate は GPL-2.0、Switchroot の L4T と Linux カーネルは GPL-2.0)。
`Library/Tegra8250SerialPortLib/Tegra8250SerialPortLib.c` は「Coreboot-ported」を名乗る(coreboot は GPL-2.0、元ファイルに表示なし)。
コードのコピーがどこまであるかは未確認。いずれも GPL-2.0 の範囲で扱う。

## 配るとき

GPL-2.0 の部分を含むので、バイナリ(`swhid.sys`、UEFI の `bl33.bin` など)を配るなら**対応するソース(このツリーとパッチ、上流の入手先)を添える**。
