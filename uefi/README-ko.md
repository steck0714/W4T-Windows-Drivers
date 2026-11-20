# uefi/ — UEFI / EDK II 쪽 개발용 소스

[日本語](README-ja.md) / [English](README.md) / [简体中文](README-zh-CN.md) / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / [Deutsch](README-de.md) / 한국어

> 일본어/영어 버전을 AI로 번역한 문서입니다. 차이가 있으면 [README-ja.md](README-ja.md)와 [README.md](README.md)가 우선합니다.

Windows 드라이버(`drivers/`)가 **EDK II(UEFI)에서 Windows를 부팅할 때 안정적으로 동작하도록** 하기 위한 UEFI 쪽 소스와
개발용 도구입니다. **펌웨어 이미지나 바이너리(BL33, bl31 등)는 포함하지 않습니다**(이는 winx 본체의 빌드 산출물입니다).
실기에서는 확인하지 않았습니다. EDK II(ARM64)로의 빌드도 아직 하지 않았습니다(아래 “확인됨 / 미확인” 참고).

## 왜 필요한가

TegraGpu의 `tg_gpu_power_up()`(`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED)은 GPU가
**리셋 중이고 클램프된 상태**(CAR `RESET_X`의 bit 24와 PMC `0x7000e6d4`의 bit 0이 모두 1)가 아니면
`TG_GPU_POWER_BUSY`를 반환하고 시작을 거부합니다(다른 쪽이 사용 중인 GPU를 가로채지 않기 위해서입니다).
OLED의 선행 구현(DanilKhmyrov/windows-switch의 `...gpu-gputestonly.patch`)도 같은 전제입니다:
“GPU는 리셋+클램프 상태로 넘기고, 전원 투입은 드라이버가 한다”. 따라서 UEFI 쪽이 할 일은
**전원을 켜는 것이 아니라, 그 상태로 Windows에 넘기는 것**입니다.

## 구성

| 경로 | 내용 |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | EDK II에 의존하지 않는 핵심(레지스터 조작만). 호스트에서 그대로 테스트할 수 있음 |
| `edk2/GpuHandoff/GpuHandoff.c` | EDK II 쪽(winx의 `PlatformBootManagerLib`에 들어감). Windows 부팅 직전의 확인과 `ExitBootServices` 시점의 재확인 |
| `edk2/GpuHandoffApp/` | UEFI Shell용 도구 `GpuHandoff.efi`(개발 중 BL33을 다시 빌드하지 않고 상태를 보고 고침). winx에서는 `Drivers/GpuHandoffApp/`로 패치에 포함됨 |
| `patches/winx-gpu-handoff.patch` | steck0714/winx의 `usually`에 적용되는 패치(`GpuHandoff.c/.h`의 배치, DSC 플래그, 호출 3곳, 호스트 테스트, Shell 도구 `Drivers/GpuHandoffApp/`와 DSC 항목) |
| `tests/gpu_handoff/` | 가짜 레지스터 모델 테스트(cold / 동작 중 / MC 정체 / 다른 SoC / 버스 오류 / 잘못된 인자)와 EDK II용 `.c`의 구문 검사 |
| `tests/shim/` | 구문 검사 전용 최소 헤더(EDK II의 대체품이 아님) |
| `tools/check_uefi_contract.py` | 레지스터·비트·HIDREV·BUSY 조건이 드라이버 쪽 전원 백엔드와 일치하는지 보는 정적 검사. `edk2/`의 복사본이 패치와 일치하는지도 확인 |

## 사용법

```
tools/run_host_tests.sh                       # 전체(uefi/ 포함)
sh uefi/tests/gpu_handoff/run.sh              # uefi/ 테스트만
python3 -I uefi/tools/check_uefi_contract.py  # 드라이버와의 정합성만
```

### winx에 적용하기

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista(SWITCH_BOARD_ERISTA와 Hekate 필요)
build ... -D SWITCH_OLED_GPU=1     # OLED(SWITCH_BOARD_OLED와 Hekate 필요)
build ... -D SWITCH_LITE_GPU=1     # Lite(기존 플래그. LiteHomeBoot에서 호출됨)
```

호출 위치: Erista / OLED는 `SwitchBootMenu.c`의 “Windows” 부팅 옵션, Lite는 `LiteHomeBoot.c`의 `BootWindowsFromMenu()`.
모두 `PlatformGpuHandoffBeforeBoot()`를 호출합니다.

### 실기에서 확인하기(UEFI Shell)

```
fs0:\> GpuHandoff.efi        읽기 전용. cold이면 Windows 드라이버가 인수할 수 있음
fs0:\> GpuHandoff.efi -fix   복원한 뒤 전후 상태를 표시
```
`GpuHandoff.efi`는 위의 GPU 플래그 중 하나를 지정했을 때만 빌드됩니다(FDF에는 넣지 않습니다. XusbInit.efi와 마찬가지로
빌드 출력에서 SD 카드로 복사합니다). 플래그 없이 하는 빌드는 아무것도 달라지지 않습니다.

## 동작 약속(GpuHandoff.h)

- 전원·PMIC·PLL에는 손대지 않습니다.
- 이미 cold이면 **아무것도 쓰지 않습니다**(몇 번 호출해도 같은 결과).
- cold가 아닐 때만 `tg_gpu_power_down()`과 같은 순서(MC 드레인 → 클램프 → 리셋 → 클록 정지)로 되돌립니다.
  **MC가 드레인되지 않으면 클램프/리셋에는 손대지 않고** 보고만 합니다(아직 버스 마스터인 GPU를 클램프하는 쪽이 더 위험하기 때문).
- 다른 SoC(HIDREV 불일치)이거나 읽기 값이 모든 비트 1이면 쓰지 않습니다.
- 어떤 실패에서도 부팅을 멈추지 않습니다.

## 확인됨 / 미확인

- 확인됨: 호스트 테스트, EDK II용 `.c`의 구문 검사(Erista / Lite / OLED 3개 설정),
  드라이버 쪽과의 레지스터·HIDREV·BUSY 조건 일치, 패치가 winx `usually`에 적용되는 것.
  정합성 검사는 드라이버 쪽 값을 망가뜨리면 실제로 실패함을 확인했습니다(Shell 도구의 복사본이나 DSC 항목이 패치와 어긋나도 실패합니다).
- 미확인: **EDK II(ARM64)로의 빌드(`GpuHandoff.efi`의 DSC 추가 포함)와 실기에서의 동작**. OLED의 호출 위치는 winx의
  `SwitchBootMenu.c`를 거치는 것을 전제로 하며, DanilKhmyrov 버전 구성에서는 위치가 다를 수 있습니다.
- 넣지 않은 것: 준비되었을 때만 ACPI의 GPU0를 보이게 하는 처리(Wi-Fi의 `WifiPrepareBoot.c` 방식), GPU 레일의 UEFI 쪽 제어.
