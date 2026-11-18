# SwitchBattery

`swbatt.sys`(`ROOT\SWBATT`): バッテリ(battc)、充電器、温度、ファン。I2C1 と PWM。

- 上流での位置: `drivers/swbatt/swbatt.c`(DanilKhmyrov/windows-switch `main`)。ここでの位置: `drivers/t214-aula/drivers/SwitchBattery/src/swbatt.c`
- 作者・出典: DanilKhmyrov/windows-switch(GPL-2.0)。Switchroot(bm92txx / bq2419x-charger、device tree)・hekate を出典としてコメントに挙げている。 ソースの先頭に**クレジットブロック**を入れた(`W4T-CREDIT-BEGIN` 〜 `END`)。
  上流のファイルに作者・著作権の行は無いので、こちらで作ってはいない。
- 上流との一致: `python tools/aula_layout.py --check`(クレジットブロックを除くと上流の SHA-256 と一致)。
- ビルド: **このリポジトリではビルドしていない**。上流の構成に戻して使う: `python tools/aula_layout.py --materialize <空のディレクトリ>` のあと、
  その中の `drivers/build.sh`(上流の README・Dockerfile の手順。EWDK と REFBIN が要る。フラグは上流に従う。こちらでは未検証)。
- 実機: windows-switch v0.8.0 の配布者 1 台(有機EL)の報告のみ。ここでは再確認していない。
- 併用不可: winx 系(`ACPI\SWOA*`)のドライバとは同じ I2C/GPIO/PWM を取り合うので同じカードに入れない(`docs/WINDOWS-SWITCH-COMPAT.md`)。
