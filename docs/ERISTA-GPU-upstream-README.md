# W4T-Erista-GPU — Switch (Erista / T210) GM20B 起動ドライバ

Lite 版 `TegraGpu`(Mariko/T214 で GR まで動作済み)を、Erista(初代 Switch、T210)向けに移植したもの。
共有コア(GM20B の MMU/FIFO/GR/ACR/PMU)はそのまま、**電源・クロック・ID・PMIC 経路だけ**を Erista 用に作り直した。

> **実機未検証。** 確認したのはホスト上のモデルテスト(全件 PASS)と ACPI 断片の整合チェックだけ。
> Windows 側(`windows/*.c`)はこの環境に WDK が無く**コンパイルしていない**。GitHub Actions で初めてコンパイルされる。

## Lite からの差分

| 項目 | Lite(T214) | Erista(T210)= このリポジトリ |
|---|---|---|
| GPU 電源 | MAX77812 M1 (I2C5 0x31/0x33) | **MAX77621 (0x1C)**、EN ピンは **MAX77620(0x3C) GPIO6**。Hekate は起動時に GPIO6 を落とすので、Windows 開始時 GPU は無給電 |
| 電源手順 | M1 設定 → クロック → リセット | GPIO6/AME 設定 → MAX77621 CONTROL1=0xB0 / CONTROL2=0xC1(Horizon 値)→ 800 mV(code 31)→ GPIO6=0x09 → 5 ms → クロック → リセット → クランプ解除 → MC |
| 電圧式 | 250 mV + 5 mV×n | **606.25 mV + 6.25 mV×n** |
| GPCPLL | NA/DVFS(C1) | **旧方式**(ロック検出付き): bypass → IDDQ 解除 → M/N/PL → enable → LOCK(bit17)待ち → SYNC → SEL_VCO |
| プロファイル | 729.6 / 1228.8 MHz | **460.8 MHz**(N=48,PL=2,1.000 V、既定)/ **768 MHz**(N=40,PL=1,1.100 V、opt-in) |
| 判定 | HIDREV major 2 / chip 0x12e | HIDREV major 1(`0x2110`)/ chip **0x12b** |
| Windows の PMIC 経路 | SwitchFan 経由のブリッジ | **ACPI の I2C 接続(SPB / Resource Hub)を GPU 自身が開く**。SwitchFan 改造不要 |
| ACPI HWID | `SWOA0F01` | **`SWOA0F02`** |

根拠: PMIC のレジスタ・値・GPIO6 は **CTCaer/hekate**(`bdk/power/max7762x.[ch]`, `max77620.h`, `hw_init.c`)で照合。
クロック手順は **Switchroot nvgpu `clk_gm20b.c`**(B1 側)。クロック/リセット/クランプ/MC の順序は Lite 実装(Switchroot `platform_gk20a_tegra.c`)と同じ。

## 使う前に(重要)

- **電圧は実機の CVB/speedo 表で裏取りしていない保守的な固定値**(460.8 MHz=1.000 V、768 MHz=1.100 V)。上限は Tegra X1 の GPU 許容内だが、調整が要る可能性あり。
- **温度ガードなし**(TMP451 は FAN0 が占有)。長時間の負荷試験はしないこと。
- PLL の LOCK ビット位置(bit17)、`CONTROL1/2` の値、GPIO6 の設定値(0x09)は Switchroot/Hekate の記憶と照合ベース。実機の最初のログで外れていたら、`stage` と `J_*` の値で場所が分かる。
- NVK 側(mesa-switch)がチップ ID 0x12b をそのまま受けるかは未確認(`query.c`/`kmt.c`/`nak_shader_compile.c` は 0x12b に変更済み)。
- NVIDIA のファームウェア 9 ファイルはリポジトリに入れていない(下記)。

## ACPI(WinX の DSDT)

    python3 acpi/t210/apply_gpu_patch.py path/to/Dsdt.winx-usually.asl   # 冪等
    # DSDT を -DSWITCH_ERISTA_GPU=1 でビルド(割り込みは試験後に -DSWITCH_ERISTA_GPU_IRQ=1)

変更は 3 点(すべて `#ifdef SWITCH_ERISTA_GPU` 内):
1. `CPUC` の CAR 窓を `0x60006000/0x1000` → `/0x100`(switchcpu.sys は 0x020/0x0E0/0x0E4 しか触らない。全ページ占有は GPU の 0x60006280・0x6000667C と衝突)
2. `PWRB` の MAX77620(0x3C)接続を Exclusive → **Shared**(GPU0 も GPIO6 のため使う)
3. `GPU0`(`SWOA0F02`)を追加。**I2C 接続の順番が ABI**: `[0]=0x1C(GPU 電源)`, `[1]=0x3C(MAX77620)`

## GitHub でビルド

`.github/workflows/build-erista-gpu.yml`(`workflow_dispatch`)。

1. `host`(ubuntu): 全ホストテスト(power / GPCPLL / core …)+ ACPI 断片の iasl 構文チェック。WDK・ファームウェア不要。
2. `driver`(windows-2022): clang-cl + lld-link + WDK 10.0.19041。`TegraGpuCore.lib` は常に作る。
   ファームウェアがあれば KMDF の `tegragpuprobe.sys` を作り、テスト署名+カタログまで行う。

ファームウェア: 次の 9 ファイルを zip のルートに入れ、Secret `NVIDIA_FW_URL`(または入力 `firmware_url`)に URL を設定。
`NETB_img.bin fecs.bin gpccs.bin nv_acr_ucode_prod.bin pmu_bl.bin gpmu_ucode_image.bin gpmu_ucode_desc.bin pmu_sig.bin fecs_sig.bin`
署名: Secret `SWOA_PFX_BASE64` / `SWOA_PFX_PASSWORD`(無ければ毎回自己署名。.cer は成果物に入る)。

ローカルでホストテストだけ: `python3 drivers/t210-icosa/drivers/TegraGpu/tools/build.py --mesa <mesa-switch@0ff52079> --out build --host-only`

## 実機での最初の手順

1. DSDT を差し替え、`SWOA0F02` が認識されることを確認。
2. `tegragpuprobe` を入れ、既定(460.8 MHz)で実行。結果の `stage`(100+電源段 / 200+クロック段 / 300+実行段)で止まった場所が分かる。
   - 100 台で止まる → 電源(GPIO6 / MAX77621)。`power_result` を見る。
   - 203〜204 で止まる → PLL ロック失敗(`CALIBRATION_ERROR`)。
   - `measured_khz` が目標の ±0.5% 外 → `COUNTER_ERROR`。
3. 通ったら 768 MHz(`SelectedKhz=0xBB800`)。

## 未完・既知の穴

- Windows 側の SPB 呼び出し(`windows/platform_io.c`)は未コンパイル・未実行。旧 `test_platform_windows.c`(SwitchFan ブリッジ前提)は削除した。SPB 版のテストは未作成。
- 共有コアに Lite 固有の前提が残っていないかの網羅確認は、ホストテストが通る範囲までしかしていない(TPC 構成は GM20B 共通のはず)。
- 割り込み(LIC 転送)・WDDM アダプタ統合は Lite と同じ未完状態。

ライセンス・出典は `drivers/t210-icosa/drivers/TegraGpu/NOTICE` と各ファイル先頭を参照(Hekate / Switchroot は GPL-2.0)。
