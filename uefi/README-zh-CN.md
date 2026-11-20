# uefi/ — UEFI / EDK II 侧的开发用源码

[日本語](README-ja.md) / [English](README.md) / 简体中文 / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / [Deutsch](README-de.md) / [한국어](README-ko.md)

> 本文由 AI 翻译自日语/英语版本。如有出入，以 [README-ja.md](README-ja.md) 和 [README.md](README.md) 为准。

这里是 UEFI 侧的源码和开发工具，用来让 Windows 驱动(`drivers/`)在**从 EDK II(UEFI)启动 Windows 时可靠地工作**。
**不包含任何固件镜像或二进制文件(BL33、bl31 等)**，它们是 winx 本体的构建产物。
尚未在真机上验证，也还没有用 EDK II(ARM64)构建过(见下文“已验证 / 未验证”)。

## 为什么需要

TegraGpu 的 `tg_gpu_power_up()`(`platform/t210_power.c` = Erista，`platform/t214_power.c` = Lite / OLED)要求 GPU
**处于复位且已钳位(clamp)状态**(CAR `RESET_X` 的 bit 24 和 PMC `0x7000e6d4` 的 bit 0 都为 1)，否则会返回
`TG_GPU_POWER_BUSY` 并拒绝启动(以免抢占正在被别人使用的 GPU)。
OLED 的上游实现(DanilKhmyrov/windows-switch 中的 `...gpu-gputestonly.patch`)前提相同:
“GPU 保持复位+钳位交出，上电由驱动完成”。因此 UEFI 侧的工作**不是给 GPU 上电，而是以这个状态把它交给 Windows**。

## 目录结构

| 路径 | 内容 |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | 不依赖 EDK II 的核心(仅寄存器操作)。可直接在主机上测试 |
| `edk2/GpuHandoff/GpuHandoff.c` | EDK II 侧(放入 winx 的 `PlatformBootManagerLib`)。Windows 启动前检查，并在 `ExitBootServices` 时再次检查 |
| `edk2/GpuHandoffApp/` | UEFI Shell 工具 `GpuHandoff.efi`(开发时无需重建 BL33 即可查看 / 修复状态)。在 winx 中以 `Drivers/GpuHandoffApp/` 的形式包含在补丁里 |
| `patches/winx-gpu-handoff.patch` | 适用于 steck0714/winx 的 `usually` 分支的补丁(`GpuHandoff.c/.h` 的放置、DSC 标志、3 处调用、主机测试、Shell 工具 `Drivers/GpuHandoffApp/` 及其 DSC 条目) |
| `tests/gpu_handoff/` | 假寄存器模型测试(冷态 / 运行中 / MC 卡住 / 其他 SoC / 总线错误 / 参数错误)，以及 EDK II 用 `.c` 的语法检查 |
| `tests/shim/` | 仅用于语法检查的最小头文件(不是 EDK II 的替代品) |
| `tools/check_uefi_contract.py` | 静态检查寄存器、位、HIDREV、BUSY 条件是否与驱动侧电源后端一致。同时检查 `edk2/` 中的副本是否与补丁一致 |

## 用法

```
tools/run_host_tests.sh                       # 全部(包含 uefi/)
sh uefi/tests/gpu_handoff/run.sh              # 仅 uefi/ 的测试
python3 -I uefi/tools/check_uefi_contract.py  # 仅检查与驱动的一致性
```

### 应用到 winx

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista(需要 SWITCH_BOARD_ERISTA 和 Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED(需要 SWITCH_BOARD_OLED 和 Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite(已有标志；由 LiteHomeBoot 调用)
```

调用位置: Erista / OLED 在 `SwitchBootMenu.c` 的“Windows”启动项，Lite 在 `LiteHomeBoot.c` 的 `BootWindowsFromMenu()`。
全部调用 `PlatformGpuHandoffBeforeBoot()`。

### 在真机上查看(UEFI Shell)

```
fs0:\> GpuHandoff.efi        只读。如果是 cold，Windows 驱动就能接管
fs0:\> GpuHandoff.efi -fix   先恢复，再显示前后状态
```
`GpuHandoff.efi` 只在设置了上述任一 GPU 标志时才会构建。它不放入 FDF；和 XusbInit.efi 一样，从构建输出复制到 SD 卡。
不带这些标志的构建不受任何影响。

## 行为承诺(GpuHandoff.h)

- 不触碰电源、PMIC 和 PLL。
- 如果已经是 cold，**不写入任何内容**(重复调用结果相同)。
- 只有在不是 cold 时，才按与 `tg_gpu_power_down()` 相同的顺序(MC 排空 → 钳位 → 复位 → 停止时钟)恢复。
  **如果 MC 排空不了，则不碰钳位/复位**，只做报告(钳位一个仍是总线主设备的 GPU 更危险)。
- 其他 SoC(HIDREV 不匹配)或读取结果为全 1 时不写入。
- 任何失败都不会中断启动。

## 已验证 / 未验证

- 已验证: 主机测试、EDK II 用 `.c` 的语法检查(Erista / Lite / OLED 三种配置)、与驱动侧在寄存器、HIDREV、BUSY 条件上的一致性、
  补丁可应用到 winx 的 `usually`。已确认一致性检查在驱动侧数值被改坏时确实会失败(Shell 工具的副本或 DSC 条目与补丁不一致时也会失败)。
- 未验证: **使用 EDK II(ARM64)构建(包括 `GpuHandoff.efi` 的 DSC 条目)，以及真机上的行为**。OLED 的调用位置以 winx 的
  `SwitchBootMenu.c` 为前提，DanilKhmyrov 版本的结构里位置可能不同。
- 未包含: 仅在准备就绪时才暴露 ACPI 的 GPU0(Wi-Fi 的 `WifiPrepareBoot.c` 方式)，以及 UEFI 侧对 GPU 供电轨的控制。
