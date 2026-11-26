# t214-aula/upstream-src のライセンス監査(ファイルごと)

対象: 上流の 110 ファイル(うち 7 ファイルは `drivers/<ドライバ名>/` へ移動済み。表の位置は**上流での位置**で、移動先は `layout.json`)(`patches/` の 27 本、`drivers/nvk/tgd3d-local.patch` の 1 本、それ以外の 82 本)。
方針: **各ファイルの先頭の表示を最優先**し、無いものは上流プロジェクトの宣言(`upstream-src/README.md` の License 節『shim・ドライバ・スクリプト・パッチは GPL-2.0、`LICENSE`』)に従う。上流の表示は書き換えていない。

**確認した範囲と限界**: 見たのは、(1) ファイル先頭 30 行の SPDX / Copyright / ライセンス文、(2) パッチが当たる NintendoSwitchPkg(imbushuo、commit `09c3ade`)の元ファイル先頭、(3) edk2 stable202102 の `LaffStd.c` 先頭。ファイルの**中身の由来**(他プロジェクトからのコピー)までは追っていない。『表示なし』は『権利なし』ではない。弁護士の判断ではない。GPL-2.0 の『のみ / 以降』は上流が書いていないので、どちらとも言えない(このリポジトリでは `GPL-2.0` としか言わない)。

## 1. パッチ以外の 82 ファイル(+ §2 の 1 本)

### 1.1 先頭にライセンスや出典の表示があるもの(11)

| ファイル | 表示 | 扱い |
|---|---|---|
| `drivers/switchwifi/switchwifi.inf` | ruje0504-2/Windows-for-Switch-Drivers `drivers/SwitchWifi/package/switchwifi.inf`(GPL-2.0)を元に文字列だけ変更 | GPL-2.0。元の著作者の表示を残す(`CREDITS.md`) |
| `drivers/switchbt/switchbt.inf` | 同 `drivers/SwitchBluetooth`(GPL-2.0)由来 | 同上 |
| `drivers/switchaudio/switchaudio.inf` | 同 `drivers/SwitchAudio`(GPL-2.0)由来 | 同上 |
| `drivers/build_wifi.sh` | 同 SwitchWifi(GPL-2.0)をビルドする手順 | GPL-2.0(スクリプト自体は上流の GPL-2.0) |
| `drivers/build_bt.sh` | 同 SwitchBluetooth(GPL-2.0) | 同上 |
| `drivers/build_audio.sh` | 同 SwitchAudio(GPL-2.0) | 同上 |
| `drivers/build_gpu.sh` | ruje0504-2/mesa-switch-woa `tegra-x1-wddm/`(**MIT と GPL-2.0 が混在**)を取り込む手順 | スクリプトは GPL-2.0。**取り込む先のファイルは SPDX がファイルごとに違う**ので、そのまま GPL-2.0 とみなさない |
| `drivers/nvk/Dockerfile` | llvm-mingw(mstorsjo)の配布物をダウンロード | ダウンロード先は各自のライセンス。ここには含まれない |
| `uefi/container-build.sh` | imbushuo/NintendoSwitchPkg を clone | 取得物は NintendoSwitchPkg のライセンスに従う(§3) |
| `README.md` | DanilKhmyrov が所有と記載。Credits 節あり | GPL-2.0(上流の宣言)。Credits はそのまま保つ |
| `docs/notes-for-mesa-switch-woa.md` | DanilKhmyrov/windows-switch への言及 | GPL-2.0(上流の宣言) |

### 1.2 先頭に表示がないもの(71)→ 上流の宣言(GPL-2.0)に従う

71 ファイル(下表)。`Switchroot` / `hekate` / `Linux` を参照するコメントが `drivers/swhid/swhid.c`・`drivers/swbatt/swbatt.c`・`shim/shim.S` にあるが、これらは**レジスタ値・手順の出典の言及**で、コードのコピーかどうかは未確認。hekate・Switchroot・Linux はいずれも GPL-2.0 なので、上流が GPL-2.0 と宣言する限り衝突はしない。

| ファイル | 備考 |
|---|---|
| `desktop/add_windows_partition.py` |  |
| `desktop/bcdlib.py` |  |
| `desktop/boot.cmd` |  |
| `desktop/boot64.cmd` |  |
| `desktop/build_w32.sh` |  |
| `desktop/build_w64.sh` |  |
| `desktop/copylogs.cmd` |  |
| `desktop/diag_card.sh` |  |
| `desktop/dumpinfo.py` |  |
| `desktop/finalize_menu.py` |  |
| `desktop/findagent64.cmd` |  |
| `desktop/finish_setup.sh` |  |
| `desktop/gpu-umdswap.ps1` |  |
| `desktop/gpudesk.ps1` |  |
| `desktop/gpuprobe.ps1` |  |
| `desktop/gpuvk.ps1` |  |
| `desktop/hive_recover.py` |  |
| `desktop/img_sums.py` |  |
| `desktop/make_bcd.sh` |  |
| `desktop/make_bcd64.sh` |  |
| `desktop/make_icon.py` |  |
| `desktop/patch_desktop.sh` |  |
| `desktop/pull_win.py` |  |
| `desktop/push_win.py` |  |
| `desktop/put_bcd.sh` |  |
| `desktop/put_bl33.sh` |  |
| `desktop/remove_win10.ps1` |  |
| `desktop/replace_extent.py` |  |
| `desktop/setup.cmd` |  |
| `desktop/setup64.cmd` |  |
| `desktop/setup_card.py` |  |
| `desktop/setup_mode.sh` |  |
| `desktop/setupwatch.cmd` |  |
| `desktop/skip_bcd_step.sh` |  |
| `desktop/ssh_setup.ps1` |  |
| `desktop/swagentsvc.c` |  |
| `desktop/sync_from_card.py` |  |
| `desktop/tune.ps1` |  |
| `desktop/unattend.xml` |  |
| `desktop/unattend64.xml` |  |
| `desktop/uwp.ps1` |  |
| `desktop/vk-triangle-preview.ps1` |  |
| `desktop/vk-triangle.cmd` |  |
| `desktop/w32.cmd` |  |
| `desktop/w64.cmd` |  |
| `desktop/wifi_clean.ps1` |  |
| `desktop/wifi_sign.ps1` |  |
| `uefi/Dockerfile` |  |
| `uefi/build.sh` |  |
| `uefi/build_bl33.sh` |  |
| `drivers/Dockerfile` |  |
| `drivers/build.sh` |  |
| `drivers/build_arm64.sh` |  |
| `drivers/build_gpu_probe.sh` |  |
| `drivers/compat.c` |  |
| `drivers/swbatt/swbatt.c` |  |
| `drivers/nvk/build.sh` |  |
| `drivers/nvk/build_desktop.sh` |  |
| `drivers/nvk/windows-arm64.ini` |  |
| `drivers/nvk/bindgen-include/endian.h` |  |
| `drivers/test-cert/test.crt` | 公開証明書(データ。秘密鍵ではない) |
| `drivers/test-cert/wifi-chain.crt` | 公開証明書(データ。秘密鍵ではない) |
| `drivers/test-cert/wifi-root.crt` | 公開証明書(データ。秘密鍵ではない) |
| `drivers/test-cert/wifi.crt` | 公開証明書(データ。秘密鍵ではない) |
| `drivers/swhid/swhid.c` |  |
| `drivers/touchread/touchread.c` |  |
| `shim/a32.S` |  |
| `shim/build.py` |  |
| `shim/shim.S` |  |
| `shim/test_shim.py` |  |
| `docs/uhs-plan.md` |  |

## 2. `drivers/nvk/tgd3d-local.patch`

`tegra-x1-wddm/windows/adapter.c`(ruje0504-2/mesa-switch-woa)への差分(1 ファイル、起動手順の失敗位置を記録する追加)。**元ファイルの SPDX を確認していない**(元ファイルはここに無い)。build_gpu.sh の説明では当該ツリーは MIT と GPL-2.0 の混在。この差分の追加行は上流の GPL-2.0 だが、当たる先のファイルのライセンスが優先される。

## 3. `patches/` の 27 本

パッチ自体(追加した行)は上流が **GPL-2.0** と宣言。当てる先は NintendoSwitchPkg(imbushuo)で、**ファイルごとにライセンスが違う**。パッチは元ファイルの表示を消さない(`wifi-win11only` は `Dsdt.asl` を全行書き換えるが、BSD の表示は同じ文面で残っている=確認済み)。

### 3.1 パッチが触る NintendoSwitchPkg のファイル(元ファイルの表示)

| ファイル | 元ファイルのライセンス | 根拠・注意 | 触るパッチ数 |
|---|---|---|---|
| `AcpiTables/Dsdt/Dsdt.asl` | BSD License | Copyright 2014-2018 ARM Ltd., 2019-2020 Bingxing Wang。Surface RT 2 の DSDT ダンプ由来の部分あり | 9 |
| `Drivers/PinMuxDxe/PinMux.c` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | ファイルに著作権表示なし | 1 |
| `Drivers/SdMmcDxe/SdMmc.c` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | ファイルに著作権表示なし | 15 |
| `Drivers/SdMmcDxe/SdMmcDxe.inf` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | ファイルに著作権表示なし | 1 |
| `Drivers/SimpleFbDxe/SimpleFbDxe.c` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | enum video_log2_bpp の変換コメントがあり、U-Boot / Linux 系の断片を含む可能性(出自未確認) | 1 |
| `Include/Device/EarlyMark.h` | 新規(パッチ内) | 表示なし → パッチ作者の GPL-2.0 | 3 |
| `Include/Device/MemoryMap.h` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | ファイルに著作権表示なし | 3 |
| `Include/Foundation/Types.h` | GPL-2.0(GNU GPL version 2) | Copyright 2018 naehrwert。ファイル先頭に全文表示あり | 1 |
| `Include/Shim/Kernel.h` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | Linux カーネルの kernel.h 風のマクロ。Linux 由来かは未確認 | 1 |
| `Library/MemoryInitPeiLib/MemoryInitPeiLib.c` | BSD License | Copyright 2011-2015 ARM Limited, 2017-2018 Andrei Warkentin | 1 |
| `Library/NintendoSwitchPlatformLib/Arm/ArmPlatformHelper.S` | 新規(パッチ内) | コメントが「ArmPlatformHelper.S の 32 ビット版」。元の AArch64 版は BSD License(ARM Ltd)なので、その派生として BSD の表示を保つこと | 3 |
| `Library/NintendoSwitchPlatformLib/NintendoSwitchPlatformLib.inf` | BSD License | Copyright 2011-2016 ARM / Linaro, 2017-2018 Warkentin | 1 |
| `Library/NintendoSwitchPlatformLib/Switch.c` | BSD License | Copyright 2011-2016 ARM / Red Hat / Linaro | 1 |
| `Library/Tegra8250SerialPortLib/FbFont.h` | 新規(パッチ内) | edk2 MdeModulePkg の LaffStd.c(Copyright 2006-2008 Intel Corporation、BSD-2-Clause-Patent)由来のフォント。パッチ内の表示は『LaffStd.c 由来(BSD-2-Clause-Patent)』の 1 行のみで、Intel の著作権表示が欠けている → `NOTICE.md` で補う | 1 |
| `Library/Tegra8250SerialPortLib/Tegra8250SerialPortLib.c` | 表示なし → 上流リポジトリの LICENSE(GPL-2.0) | 先頭コメントが「Coreboot-ported」。coreboot(GPL-2.0)由来の可能性、ファイルに表示なし | 3 |
| `NintendoSwitch.dsc` | BSD License | Copyright 2011-2016 ARM / Linaro / Intel, 2018-2020 Bingxing Wang | 5 |

パッチが生む UEFI 全体は、BSD(ARM / Linaro / Intel)と GPL-2.0(naehrwert の `Types.h`、パッチ追加分、リポジトリ本体)が混ざるので、**配るバイナリは GPL-2.0 として扱う**(BSD は GPL-2.0 と両立)。

### 3.2 パッチごとの対象ファイル

| パッチ | 触るファイル |
|---|---|
| `nintendoswitchpkg-hekate.patch` | `Include/Device/MemoryMap.h`, `NintendoSwitch.dsc` |
| `nintendoswitchpkg-sd-power.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zarm32.patch` | `Drivers/PinMuxDxe/PinMux.c`, `Drivers/SimpleFbDxe/SimpleFbDxe.c`, `Include/Device/EarlyMark.h`, `Include/Device/MemoryMap.h`, `Include/Foundation/Types.h`, `Include/Shim/Kernel.h`, `Library/MemoryInitPeiLib/MemoryInitPeiLib.c`, `Library/NintendoSwitchPlatformLib/Arm/ArmPlatformHelper.S`, `Library/NintendoSwitchPlatformLib/NintendoSwitchPlatformLib.inf`, `Library/NintendoSwitchPlatformLib/Switch.c`, `Library/Tegra8250SerialPortLib/FbFont.h`, `Library/Tegra8250SerialPortLib/Tegra8250SerialPortLib.c`, `NintendoSwitch.dsc` |
| `nintendoswitchpkg-zsdhost.patch` | `AcpiTables/Dsdt/Dsdt.asl`, `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzfan.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzztouch.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzztouchacpi.patch` | `AcpiTables/Dsdt/Dsdt.asl` |
| `nintendoswitchpkg-zzzzzel1-a64only.patch` | `NintendoSwitch.dsc` |
| `nintendoswitchpkg-zzzzzzshim-a64only.patch` | `Include/Device/MemoryMap.h` |
| `nintendoswitchpkg-zzzzzzzclean.patch` | `Drivers/SdMmcDxe/SdMmc.c`, `Include/Device/EarlyMark.h`, `Library/NintendoSwitchPlatformLib/Arm/ArmPlatformHelper.S`, `Library/Tegra8250SerialPortLib/Tegra8250SerialPortLib.c`, `NintendoSwitch.dsc` |
| `nintendoswitchpkg-zzzzzzzzram408.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzclean-cleanonly.patch` | `Drivers/SdMmcDxe/SdMmc.c`, `Include/Device/EarlyMark.h`, `Library/NintendoSwitchPlatformLib/Arm/ArmPlatformHelper.S`, `Library/Tegra8250SerialPortLib/Tegra8250SerialPortLib.c` |
| `nintendoswitchpkg-zzzzzzzzzcpu1428.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzdiag.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzbpmp.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzzebs-a64only.patch` | `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzzzrtnull-a64only.patch` | `NintendoSwitch.dsc` |
| `nintendoswitchpkg-zzzzzzzzzzzzzznouart-a64only.patch` | `AcpiTables/Dsdt/Dsdt.asl` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzcca-a64only.patch` |  |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzbootopt-a64only.patch` |  |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzwin11-win11only.patch` |  |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzwifi-win11only.patch` | `AcpiTables/Dsdt/Dsdt.asl`, `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzzbluetooth-win11only.patch` | `AcpiTables/Dsdt/Dsdt.asl`, `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzzzaudio-win11only.patch` | `AcpiTables/Dsdt/Dsdt.asl`, `Drivers/SdMmcDxe/SdMmc.c` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzzzzusb-win11only.patch` | `AcpiTables/Dsdt/Dsdt.asl`, `Drivers/SdMmcDxe/SdMmc.c`, `Drivers/SdMmcDxe/SdMmcDxe.inf` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzzzzzgpu-gputestonly.patch` | `AcpiTables/Dsdt/Dsdt.asl` |
| `nintendoswitchpkg-zzzzzzzzzzzzzzzzzzzzzzzzgpudisplay-gpudisplayonly.patch` | `AcpiTables/Dsdt/Dsdt.asl` |

## 4. 見つかった問題と対応

1. **`FbFont.h`(`zarm32` が追加)に、元の Intel の著作権表示が無い。** BSD-2-Clause-Patent は著作権表示の保持を求める。上流のパッチは書き換えない(無改変の方針)ので、`NOTICE.md` に Intel の表示と全文の参照(`LICENSES/BSD-2-Clause-Patent.txt`)を置いた。上流には報告して直してもらうのが筋。
2. **`ArmPlatformHelper.S`(32 ビット版、`zarm32` が追加)**は edk2 / ARM の AArch64 版を元にしている旨のコメントがあり、元は BSD(ARM Ltd)。同様に `NOTICE.md` に帰属を置いた。
3. **`Tegra8250SerialPortLib.c` が『Coreboot-ported』**と自称するが、元ファイルにライセンス表示が無い。coreboot は GPL-2.0 なので、上流の GPL-2.0 宣言と矛盾はしないが、由来の確認はしていない。
4. **`Include/Foundation/Types.h` は GPL-2.0(naehrwert)**で、BSD ではない。パッチが当たったあとの UEFI は GPL-2.0 の義務(ソース提供)を伴う。配布するなら対応するソースとパッチを添えること。
5. **`drivers/build_gpu.sh` と `tgd3d-local.patch`** は MIT / GPL-2.0 が混在する mesa-switch-woa のファイルを扱う。取り込んだ後のファイルごとの SPDX を消さないこと。
6. 3 つの INF は ruje0504-2 の GPL-2.0 ドライバ由来(文字列のみ変更)。元の著作者の表示は `CREDITS.md` に維持。
7. **未確認**: パッチの追加行が hekate / Switchroot Linux / winx の**コードのコピー**かどうか。コメントは『手順に従った』『ポートした』とあり、一部(winx の WifiPcie.h など)は明示的に GPL-2.0 のコードの移植。いずれも GPL-2.0 なので、**パッチを GPL-2.0 で配ることは整合する**が、BSD 側へ移すことはできない。

## 3. 上流の画像・バイナリ(置いてあるが配布物には含めない)

`kept-binaries.json` に SHA-256 を固定した 6 点。上流 main にあったものをそのまま置いている。ライセンス表示はファイル内に無く、上流の宣言(GPL-2.0)に従うものとして扱うが、画像の権利までは確認していない。

- `desktop/swagentsvc.exe` — 上流のビルド済み swagentsvc.exe(manifest/windows-switch-0.8.0.json の v0.8.0 と同一のもの)
- `desktop/icon_windows.bmp` — 上流のアイコン画像
- `docs/images/gpu-desktop-taskmanager.jpg` — 上流 docs のスクリーンショット
- `docs/screenshots/arm64-desktop.jpg` — 上流 docs のスクリーンショット
- `docs/screenshots/win11-arm64-desktop.jpg` — 上流 docs のスクリーンショット
- `docs/screenshots/arm32-desktop.jpg` — 上流 docs のスクリーンショット
