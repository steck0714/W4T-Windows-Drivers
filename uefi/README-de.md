# uefi/ — Entwicklungsquellen für die UEFI-/EDK-II-Seite

[日本語](README-ja.md) / [English](README.md) / [简体中文](README-zh-CN.md) / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / [Français](README-fr.md) / Deutsch / [한국어](README-ko.md)

> KI-generierte Übersetzung der japanischen und englischen Fassung. Im Zweifel gelten [README-ja.md](README-ja.md) und [README.md](README.md).

Quellen und Entwicklungswerkzeuge auf der UEFI-Seite, damit die Windows-Treiber (`drivers/`) **zuverlässig funktionieren, wenn
Windows aus EDK II (UEFI) gestartet wird**. **Es sind keine Firmware-Images oder Binärdateien (BL33, bl31, ...) enthalten**; das sind
Build-Ergebnisse von winx selbst. Nicht auf echter Hardware getestet. Ebenfalls noch nicht mit EDK II (ARM64) gebaut
(siehe „Geprüft / nicht geprüft“ unten).

## Warum das nötig ist

`tg_gpu_power_up()` von TegraGpu (`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED) startet nicht und gibt
`TG_GPU_POWER_BUSY` zurück, wenn die GPU nicht **im Reset und geklemmt** ist (Bit 24 von CAR `RESET_X` und Bit 0 von
PMC `0x7000e6d4` sind beide 1). So übernimmt der Treiber keine GPU, die jemand anderes gerade nutzt.
Die OLED-Referenzimplementierung (`...gpu-gputestonly.patch` in DanilKhmyrov/windows-switch) geht von derselben Voraussetzung aus:
„Die GPU wird im Reset und geklemmt übergeben; der Treiber schaltet sie ein.“ Die Aufgabe der UEFI-Seite ist daher **nicht, die GPU
einzuschalten, sondern sie in diesem Zustand an Windows zu übergeben**.

## Aufbau

| Pfad | Inhalt |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | EDK-II-unabhängiger Kern (nur Registerzugriffe). Lässt sich direkt auf einem Host testen |
| `edk2/GpuHandoff/GpuHandoff.c` | EDK-II-Seite (kommt in winx' `PlatformBootManagerLib`). Prüft unmittelbar vor dem Windows-Start und erneut bei `ExitBootServices` |
| `edk2/GpuHandoffApp/` | UEFI-Shell-Werkzeug `GpuHandoff.efi` (Zustand während der Entwicklung ansehen / korrigieren, ohne BL33 neu zu bauen). In winx steckt es als `Drivers/GpuHandoffApp/` im Patch |
| `patches/winx-gpu-handoff.patch` | Patch für `usually` in steck0714/winx (Platzierung von `GpuHandoff.c/.h`, DSC-Flags, 3 Aufrufstellen, Host-Test, das Shell-Werkzeug `Drivers/GpuHandoffApp/` samt DSC-Einträgen) |
| `tests/gpu_handoff/` | Test mit einem Fake-Registermodell (kalt / läuft / MC hängt / anderer SoC / Busfehler / ungültiges Argument) sowie Syntaxprüfung der EDK-II-`.c`-Dateien |
| `tests/shim/` | Minimale Header nur für die Syntaxprüfung (kein Ersatz für EDK II) |
| `tools/check_uefi_contract.py` | Statische Prüfung, ob Register, Bits, HIDREV und BUSY-Bedingung mit den Power-Backends des Treibers übereinstimmen. Prüft auch, ob die Kopien in `edk2/` dem Patch entsprechen |

## Verwendung

```
tools/run_host_tests.sh                       # alles (einschließlich uefi/)
sh uefi/tests/gpu_handoff/run.sh              # nur die Tests in uefi/
python3 -I uefi/tools/check_uefi_contract.py  # nur die Übereinstimmung mit den Treibern
```

### In winx einspielen

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista (benötigt SWITCH_BOARD_ERISTA und Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED (benötigt SWITCH_BOARD_OLED und Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite (bestehendes Flag; wird von LiteHomeBoot aufgerufen)
```

Aufrufstellen: bei Erista / OLED die Boot-Option „Windows“ in `SwitchBootMenu.c`; bei Lite `BootWindowsFromMenu()` in
`LiteHomeBoot.c`. Alle rufen `PlatformGpuHandoffBeforeBoot()` auf.

### Prüfung am echten Gerät (UEFI Shell)

```
fs0:\> GpuHandoff.efi        nur lesen. Ist die GPU cold, kann der Windows-Treiber sie übernehmen
fs0:\> GpuHandoff.efi -fix   wiederherstellen, dann Zustand davor und danach anzeigen
```
`GpuHandoff.efi` wird nur gebaut, wenn eines der obigen GPU-Flags gesetzt ist. Es kommt nicht ins FDF; wie
XusbInit.efi wird es aus der Build-Ausgabe auf die SD-Karte kopiert. Ein Build ohne die Flags bleibt unverändert.

## Verhaltenszusagen (GpuHandoff.h)

- Fasst weder Stromversorgung noch PMIC noch irgendeine PLL an.
- Ist die GPU bereits cold, wird **nichts geschrieben** (mehrfache Aufrufe liefern dasselbe Ergebnis).
- Nur wenn sie nicht cold ist, wird sie in derselben Reihenfolge wie bei `tg_gpu_power_down()` zurückgesetzt
  (MC leeren, Clamp, Reset, Takt stoppen). **Leert sich der MC nicht, bleiben Clamp und Reset unangetastet** und es wird nur gemeldet
  (eine GPU zu klemmen, die noch Busmaster ist, wäre gefährlicher).
- Bei einem anderen SoC (HIDREV passt nicht) oder wenn ein Lesezugriff nur Einsen liefert, wird nichts geschrieben.
- Kein Fehler hält den Bootvorgang an.

## Geprüft / nicht geprüft

- Geprüft: Host-Tests, Syntaxprüfung der EDK-II-`.c`-Dateien (Konfigurationen Erista / Lite / OLED),
  Übereinstimmung mit der Treiberseite bei Registern, HIDREV und BUSY-Bedingung sowie dass sich der Patch auf winx `usually` anwenden lässt.
  Es wurde bestätigt, dass die Konsistenzprüfung tatsächlich fehlschlägt, wenn ein Wert auf der Treiberseite kaputtgemacht wird (sie schlägt auch fehl,
  wenn die Kopie des Shell-Werkzeugs oder die DSC-Einträge vom Patch abweichen).
- Nicht geprüft: **der Build mit EDK II (ARM64) (einschließlich der DSC-Einträge für `GpuHandoff.efi`) und das Verhalten auf echter Hardware**.
  Die OLED-Aufrufstelle setzt winx' `SwitchBootMenu.c` voraus; in der Struktur von DanilKhmyrov kann sie woanders liegen.
- Nicht enthalten: ACPI-GPU0 nur dann sichtbar machen, wenn bereit (Ansatz von `WifiPrepareBoot.c` beim WLAN), sowie UEFI-seitige Steuerung der GPU-Spannungsschienen.
