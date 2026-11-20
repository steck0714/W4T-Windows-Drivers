# uefi/ — Fontes de desenvolvimento do lado UEFI / EDK II

[日本語](README-ja.md) / [English](README.md) / [简体中文](README-zh-CN.md) / [Español](README-es.md) / Português (Brasil) / [Français](README-fr.md) / [Deutsch](README-de.md) / [한국어](README-ko.md)

> Tradução gerada por IA a partir das versões em japonês e inglês. Em caso de dúvida, valem [README-ja.md](README-ja.md) e [README.md](README.md).

Fontes e ferramentas de desenvolvimento do lado UEFI para que os drivers do Windows (`drivers/`) **funcionem de forma confiável
quando o Windows é iniciado a partir do EDK II (UEFI)**. **Não há imagens de firmware nem binários (BL33, bl31, ...)**; eles são
saídas de compilação do próprio winx. Não testado em hardware real. Também ainda não foi compilado com o EDK II (ARM64)
(veja «Verificado / não verificado» abaixo).

## Por que isso é necessário

A função `tg_gpu_power_up()` do TegraGpu (`platform/t210_power.c` = Erista, `platform/t214_power.c` = Lite / OLED) se recusa a
iniciar e retorna `TG_GPU_POWER_BUSY` a menos que a GPU esteja **em reset e com clamp** (o bit 24 de CAR `RESET_X` e o bit 0 de
PMC `0x7000e6d4` valem 1). Isso evita que o driver assuma uma GPU que outra pessoa está usando.
A implementação OLED de origem (`...gpu-gputestonly.patch` em DanilKhmyrov/windows-switch) parte da mesma premissa:
«entregar a GPU em reset e com clamp; o driver a liga». Portanto, o trabalho do lado UEFI **não é ligar a GPU,
mas entregá-la ao Windows nesse estado**.

## Estrutura

| Caminho | Conteúdo |
|---|---|
| `edk2/GpuHandoff/GpuHandoff.h` | Núcleo independente do EDK II (apenas acesso a registradores). Pode ser testado direto em um host |
| `edk2/GpuHandoff/GpuHandoff.c` | Lado EDK II (entra no `PlatformBootManagerLib` do winx). Verifica logo antes de iniciar o Windows e de novo em `ExitBootServices` |
| `edk2/GpuHandoffApp/` | Ferramenta de UEFI Shell `GpuHandoff.efi` (ver / corrigir o estado durante o desenvolvimento sem recompilar o BL33). No winx ela vai no patch como `Drivers/GpuHandoffApp/` |
| `patches/winx-gpu-handoff.patch` | Patch para o `usually` de steck0714/winx (colocação de `GpuHandoff.c/.h`, flags do DSC, 3 pontos de chamada, teste em host, a ferramenta Shell `Drivers/GpuHandoffApp/` e suas entradas no DSC) |
| `tests/gpu_handoff/` | Teste com um modelo de registradores falso (frio / em execução / MC travado / outro SoC / erro de barramento / argumento inválido) e verificação de sintaxe dos `.c` do EDK II |
| `tests/shim/` | Cabeçalhos mínimos apenas para a verificação de sintaxe (não substituem o EDK II) |
| `tools/check_uefi_contract.py` | Verificação estática de que registradores, bits, HIDREV e a condição BUSY coincidem com os backends de energia do driver. Também confere se as cópias em `edk2/` batem com o patch |

## Uso

```
tools/run_host_tests.sh                       # tudo (inclui uefi/)
sh uefi/tests/gpu_handoff/run.sh              # apenas os testes de uefi/
python3 -I uefi/tools/check_uefi_contract.py  # apenas a consistência com os drivers
```

### Aplicando ao winx

```
cd winx && git apply ../W4T-Windows-Drivers/uefi/patches/winx-gpu-handoff.patch
build ... -D SWITCH_ERISTA_GPU=1   # Erista (requer SWITCH_BOARD_ERISTA e Hekate)
build ... -D SWITCH_OLED_GPU=1     # OLED (requer SWITCH_BOARD_OLED e Hekate)
build ... -D SWITCH_LITE_GPU=1     # Lite (flag existente; chamada pelo LiteHomeBoot)
```

Pontos de chamada: em Erista / OLED, a opção de boot «Windows» em `SwitchBootMenu.c`; em Lite, `BootWindowsFromMenu()` em
`LiteHomeBoot.c`. Todos chamam `PlatformGpuHandoffBeforeBoot()`.

### Verificando em um aparelho real (UEFI Shell)

```
fs0:\> GpuHandoff.efi        somente leitura. Se a GPU estiver cold, o driver do Windows pode assumi-la
fs0:\> GpuHandoff.efi -fix   restaura e mostra o antes e o depois
```
`GpuHandoff.efi` só é compilado quando uma das flags de GPU acima está definida. Ele não entra no FDF; assim como o
XusbInit.efi, copie-o da saída da compilação para o cartão SD. Uma compilação sem as flags não muda.

## Garantias de comportamento (GpuHandoff.h)

- Nunca mexe em alimentação, no PMIC nem em nenhum PLL.
- Se a GPU já estiver cold, **nada é escrito** (chamar várias vezes dá o mesmo resultado).
- Somente quando não está cold, restaura na mesma ordem de `tg_gpu_power_down()`
  (drenagem do MC, clamp, reset, parada de clock). **Se o MC não drenar, clamp e reset não são tocados** e só se reporta o problema
  (aplicar clamp a uma GPU que ainda é master do barramento é mais perigoso).
- Nada é escrito em outro SoC (HIDREV diferente) nem quando uma leitura retorna todos os bits em 1.
- Nenhuma falha interrompe o boot.

## Verificado / não verificado

- Verificado: testes em host, verificação de sintaxe dos `.c` do EDK II (configurações Erista / Lite / OLED),
  coincidência com o lado do driver em registradores, HIDREV e condição BUSY, e que o patch se aplica ao `usually` do winx.
  Confirmou-se que a verificação de consistência realmente falha quando um valor do driver é quebrado (também falha se a cópia da
  ferramenta Shell ou as entradas do DSC se afastarem do patch).
- Não verificado: **a compilação com o EDK II (ARM64) (incluindo as entradas do DSC de `GpuHandoff.efi`) e o comportamento em hardware real**.
  O ponto de chamada OLED pressupõe o `SwitchBootMenu.c` do winx; na estrutura do DanilKhmyrov a posição pode ser outra.
- Não incluído: expor o GPU0 do ACPI somente quando estiver pronto (o método `WifiPrepareBoot.c` do Wi-Fi) e o controle dos trilhos da GPU pelo UEFI.
