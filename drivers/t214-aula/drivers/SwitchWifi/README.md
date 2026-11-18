# SwitchWifi

Wi-Fi の INF(`switchwifi.inf`)のみ。`.sys` は winx Lite 系(0.5.7.0)の配布物で、ここにはソースが無い。

- 上流での位置: `drivers/switchwifi/switchwifi.inf`(DanilKhmyrov/windows-switch `main`)。ここでの位置: `drivers/t214-aula/drivers/SwitchWifi/switchwifi.inf`
- 作者・出典: DanilKhmyrov/windows-switch(GPL-2.0)。INF の元は ruje0504-2/Windows-for-Switch-Drivers(GPL-2.0)で、文字列だけ変えたもの(INF 内の表示を残している)。 INF は**無改変**(INF 自体の出典表示を残すため、クレジットブロックは足していない)。
- 上流との一致: `python tools/aula_layout.py --check`(クレジットブロックを除くと上流の SHA-256 と一致)。
- ビルド: **このリポジトリではビルドしていない**。上流の構成に戻して使う: `python tools/aula_layout.py --materialize <空のディレクトリ>` のあと、
  その中の `drivers/build.sh`(上流の README・Dockerfile の手順。EWDK と REFBIN が要る。フラグは上流に従う。こちらでは未検証)。
- 実機: windows-switch v0.8.0 の配布者 1 台(有機EL)の報告のみ。ここでは再確認していない。
- 併用不可: winx 系(`ACPI\SWOA*`)のドライバとは同じ I2C/GPIO/PWM を取り合うので同じカードに入れない(`docs/WINDOWS-SWITCH-COMPAT.md`)。
