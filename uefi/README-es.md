# uefi/ — Fuentes de desarrollo del lado UEFI / EDK II

[日本語](README-ja.md) / [English](README.md) / [简体中文](README-zh-CN.md) / Español / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / [Deutsch](README-de.md) / [한국어](README-ko.md)

> Traducción generada con IA a partir de las versiones en japonés e inglés. En caso de duda, prevalecen [README-ja.md](README-ja.md) y [README.md](README.md).

Fuentes y herramientas de desarrollo del lado UEFI para que los controladores de Windows (`drivers/`) **funcionen de forma fiable
cuando Windows se inicia desde EDK II (UEFI)**. **No se incluyen imágenes de firmware ni binarios (BL33, bl31, ...)**; son
resultados de compilación del propio winx. No probado en hardware real. Tampoco se ha compilado con EDK II (ARM64)
(véase «Verificado / no verificado» más abajo).

## Por qué es necesario

La función `tg_gpu_power_up()` de TegraGpu (`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED) se niega a
arrancar y devuelve `TG_GPU_POWER_BUSY` si la GPU no está **en reset y con el clamp activo** (el bit 24 de CAR `RESET_X` y el bit 0 de
PMC `0x7000e6d4` valen 1). Así el controlador no se apodera de una GPU que otro está usando.
La implementación OLED de origen (`...gpu-gputestonly.patch` en DanilKhmyrov/windows-switch) parte de la misma premisa:
«se entrega la GPU en reset y con clamp; el controlador la enciende». Por tanto, el trabajo del lado UEFI **no es encender la GPU,
sino entregarla a Windows en ese estado**.

## Estructura

| Ruta | Contenido |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | Núcleo independiente de EDK II (solo acceso a registros). Se puede probar tal cual en un host |
| `edk2/GpuHandoff/GpuHandoff.c` | Lado EDK II (va en `PlatformBootManagerLib` de winx). Comprueba justo antes de iniciar Windows y otra vez en `ExitBootServices` |
| `edk2/GpuHandoffApp/` | Herramienta de UEFI Shell `GpuHandoff.efi` (ver / corregir el estado durante el desarrollo sin recompilar BL33). En winx va en el parche como `Drivers/GpuHandoffApp/` |
| `patches/winx-gpu-handoff.patch` | Parche para `usually` de steck0714/winx (ubicación de `GpuHandoff.c/.h`, banderas DSC, 3 puntos de llamada, prueba en host, la herramienta Shell `Drivers/GpuHandoffApp/` y sus entradas DSC) |
| `tests/gpu_handoff/` | Prueba con un modelo de registros falso (frío / en marcha / MC atascado / otro SoC / error de bus / argumento inválido) y comprobación de sintaxis de los `.c` de EDK II |
| `tests/shim/` | Cabeceras mínimas solo para la comprobación de sintaxis (no sustituyen a EDK II) |
| `tools/check_uefi_contract.py` | Comprobación estática de que registros, bits, HIDREV y la condición BUSY coinciden con los backends de energía del controlador. También verifica que las copias de `edk2/` coinciden con el parche |

## Uso

```
tools/run_host_tests.sh                       # todo (incluye uefi/)
sh uefi/tests/gpu_handoff/run.sh              # solo las pruebas de uefi/
python3 -I uefi/tools/check_uefi_contract.py  # solo la coherencia con los controladores
```

### Aplicar a winx

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista (requiere SWITCH_BOARD_ERISTA y Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED (requiere SWITCH_BOARD_OLED y Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite (bandera existente; la llama LiteHomeBoot)
```

Puntos de llamada: en Erista / OLED, la opción de arranque «Windows» de `SwitchBootMenu.c`; en Lite, `BootWindowsFromMenu()` de
`LiteHomeBoot.c`. Todos llaman a `PlatformGpuHandoffBeforeBoot()`.

### Comprobación en un dispositivo real (UEFI Shell)

```
fs0:\> GpuHandoff.efi        solo lectura. Si la GPU está cold, el controlador de Windows puede tomarla
fs0:\> GpuHandoff.efi -fix   restaura y muestra el antes y el después
```
`GpuHandoff.efi` solo se compila si se define alguna de las banderas de GPU anteriores. No va en el FDF; igual que
XusbInit.efi, se copia desde la salida de compilación a la tarjeta SD. Una compilación sin las banderas no cambia.

## Garantías de comportamiento (GpuHandoff.h)

- Nunca toca la alimentación, el PMIC ni ningún PLL.
- Si la GPU ya está cold, **no escribe nada** (llamarla varias veces da el mismo resultado).
- Solo cuando no está cold, la restaura en el mismo orden que `tg_gpu_power_down()`
  (drenado del MC, clamp, reset, parada de reloj). **Si el MC no se drena, no toca clamp ni reset** y solo informa
  (aplicar clamp a una GPU que sigue siendo maestra del bus es más peligroso).
- No escribe nada con otro SoC (HIDREV distinto) ni cuando una lectura devuelve todo unos.
- Ningún fallo detiene el arranque.

## Verificado / no verificado

- Verificado: pruebas en host, comprobación de sintaxis de los `.c` de EDK II (configuraciones Erista / Lite / OLED),
  coincidencia con el lado del controlador en registros, HIDREV y condición BUSY, y que el parche se aplica a `usually` de winx.
  Se comprobó que la verificación de coherencia falla de verdad si se rompe un valor del controlador (también falla si la copia de la
  herramienta Shell o las entradas DSC se desvían del parche).
- No verificado: **la compilación con EDK II (ARM64) (incluidas las entradas DSC de `GpuHandoff.efi`) y el comportamiento en hardware real**.
  El punto de llamada OLED supone el `SwitchBootMenu.c` de winx; en la estructura de DanilKhmyrov puede estar en otro sitio.
- No incluido: mostrar GPU0 en ACPI solo cuando esté lista (el método de `WifiPrepareBoot.c` del Wi-Fi) y el control de los raíles de la GPU desde UEFI.
