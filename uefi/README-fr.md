# uefi/ — Sources de développement côté UEFI / EDK II

[日本語](README-ja.md) / [English](README.md) / [简体中文](README-zh-CN.md) / [Español](README-es.md) / [Português (Brasil)](README-pt-BR.md) / Français / [Deutsch](README-de.md) / [한국어](README-ko.md)

> Traduction générée par IA à partir des versions japonaise et anglaise. En cas de doute, [README-ja.md](README-ja.md) et [README.md](README.md) font foi.

Sources et outils de développement côté UEFI qui permettent aux pilotes Windows (`drivers/`) de **fonctionner de façon fiable
lorsque Windows est lancé depuis EDK II (UEFI)**. **Aucune image de firmware ni binaire (BL33, bl31, ...) n'est inclus** ; ce sont
des produits de compilation de winx lui-même. Non testé sur du matériel réel. Pas encore compilé avec EDK II (ARM64) non plus
(voir « Vérifié / non vérifié » plus bas).

## Pourquoi c'est nécessaire

La fonction `tg_gpu_power_up()` de TegraGpu (`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED) refuse de
démarrer et renvoie `TG_GPU_POWER_BUSY` si le GPU n'est pas **en reset et clampé** (le bit 24 de CAR `RESET_X` et le bit 0 de
PMC `0x7000e6d4` valent tous deux 1). Cela évite que le pilote ne s'empare d'un GPU utilisé par quelqu'un d'autre.
L'implémentation OLED d'origine (`...gpu-gputestonly.patch` dans DanilKhmyrov/windows-switch) part de la même prémisse :
« le GPU est remis en reset et clampé ; le pilote l'alimente ». Le rôle du côté UEFI **n'est donc pas d'allumer le GPU,
mais de le remettre à Windows dans cet état**.

## Organisation

| Chemin | Contenu |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | Cœur indépendant d'EDK II (accès aux registres uniquement). Testable tel quel sur un hôte |
| `edk2/GpuHandoff/GpuHandoff.c` | Côté EDK II (intégré à `PlatformBootManagerLib` de winx). Vérifie juste avant le lancement de Windows, puis à nouveau à `ExitBootServices` |
| `edk2/GpuHandoffApp/` | Outil UEFI Shell `GpuHandoff.efi` (voir / corriger l'état pendant le développement sans recompiler BL33). Dans winx, il est fourni par le correctif sous `Drivers/GpuHandoffApp/` |
| `patches/winx-gpu-handoff.patch` | Correctif pour la branche `usually` de steck0714/winx (emplacement de `GpuHandoff.c/.h`, drapeaux DSC, 3 points d'appel, test hôte, l'outil Shell `Drivers/GpuHandoffApp/` et ses entrées DSC) |
| `tests/gpu_handoff/` | Test avec un faux modèle de registres (froid / en marche / MC bloqué / autre SoC / erreur de bus / argument invalide) et vérification de syntaxe des `.c` EDK II |
| `tests/shim/` | En-têtes minimaux pour la seule vérification de syntaxe (ne remplacent pas EDK II) |
| `tools/check_uefi_contract.py` | Vérification statique que registres, bits, HIDREV et condition BUSY concordent avec les backends d'alimentation du pilote. Vérifie aussi que les copies de `edk2/` correspondent au correctif |

## Utilisation

```
tools/run_host_tests.sh                       # tout (y compris uefi/)
sh uefi/tests/gpu_handoff/run.sh              # uniquement les tests de uefi/
python3 -I uefi/tools/check_uefi_contract.py  # uniquement la cohérence avec les pilotes
```

### Appliquer à winx

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista (nécessite SWITCH_BOARD_ERISTA et Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED (nécessite SWITCH_BOARD_OLED et Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite (drapeau existant ; appelé par LiteHomeBoot)
```

Points d'appel : pour Erista / OLED, l'option de démarrage « Windows » de `SwitchBootMenu.c` ; pour Lite, `BootWindowsFromMenu()` dans
`LiteHomeBoot.c`. Tous appellent `PlatformGpuHandoffBeforeBoot()`.

### Vérification sur un appareil réel (UEFI Shell)

```
fs0:\> GpuHandoff.efi        lecture seule. Si le GPU est cold, le pilote Windows peut le prendre en charge
fs0:\> GpuHandoff.efi -fix   restaure, puis affiche l'avant et l'après
```
`GpuHandoff.efi` n'est compilé que si l'un des drapeaux GPU ci-dessus est défini. Il n'est pas placé dans le FDF ; comme
XusbInit.efi, copiez-le depuis la sortie de compilation vers la carte SD. Une compilation sans ces drapeaux reste inchangée.

## Garanties de comportement (GpuHandoff.h)

- Ne touche jamais à l'alimentation, au PMIC ni à aucun PLL.
- Si le GPU est déjà cold, **rien n'est écrit** (des appels répétés donnent le même résultat).
- Seulement s'il n'est pas cold, il le restaure dans le même ordre que `tg_gpu_power_down()`
  (vidage du MC, clamp, reset, arrêt de l'horloge). **Si le MC ne se vide pas, clamp et reset ne sont pas touchés** et le problème est
  seulement signalé (clamper un GPU encore maître du bus est plus dangereux).
- Rien n'est écrit pour un autre SoC (HIDREV différent) ni lorsqu'une lecture renvoie que des 1.
- Aucun échec n'interrompt le démarrage.

## Vérifié / non vérifié

- Vérifié : tests hôte, vérification de syntaxe des `.c` EDK II (configurations Erista / Lite / OLED),
  concordance avec le côté pilote pour les registres, HIDREV et la condition BUSY, et application du correctif sur `usually` de winx.
  Il a été confirmé que la vérification de cohérence échoue réellement lorsqu'une valeur côté pilote est cassée (elle échoue aussi si la copie de
  l'outil Shell ou les entrées DSC s'écartent du correctif).
- Non vérifié : **la compilation avec EDK II (ARM64) (y compris les entrées DSC de `GpuHandoff.efi`) et le comportement sur matériel réel**.
  Le point d'appel OLED suppose le `SwitchBootMenu.c` de winx ; dans l'organisation de DanilKhmyrov, il peut se trouver ailleurs.
- Non inclus : n'exposer le GPU0 d'ACPI que lorsqu'il est prêt (méthode `WifiPrepareBoot.c` du Wi-Fi) et le contrôle des rails du GPU côté UEFI.
