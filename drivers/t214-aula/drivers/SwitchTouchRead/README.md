# SwitchTouchRead

`touchread.c`: swhid が取り込む FTS4 タッチパネルの I2C3 アクセスと初期化。単体ビルドでは 1 分間タッチを `D:\agent\touch.txt` に記録する試験ドライバ。`swhid.c` が `#include` する(単独の driver ではない)。

- 上流での位置: `drivers/touchread/touchread.c`(DanilKhmyrov/windows-switch `main`)。ここでの位置: `drivers/t214-aula/drivers/SwitchTouchRead/src/touchread.c`
- 作者・出典: DanilKhmyrov/windows-switch(GPL-2.0)。Switchroot・hekate のタッチ電源手順。 ソースの先頭に**クレジットブロック**を入れた(`W4T-CREDIT-BEGIN` 〜 `END`)。
  上流のファイルに作者・著作権の行は無いので、こちらで作ってはいない。
- 上流との一致: `python tools/aula_layout.py --check`(クレジットブロックを除くと上流の SHA-256 と一致)。
- ビルド: **このリポジトリではビルドしていない**。上流の構成に戻して使う: `python tools/aula_layout.py --materialize <空のディレクトリ>` のあと、
  その中の `drivers/build.sh`(上流の README・Dockerfile の手順。EWDK と REFBIN が要る。フラグは上流に従う。こちらでは未検証)。
- 実機: windows-switch v0.8.0 の配布者 1 台(有機EL)の報告のみ。ここでは再確認していない。
- 併用不可: winx 系(`ACPI\SWOA*`)のドライバとは同じ I2C/GPIO/PWM を取り合うので同じカードに入れない(`docs/WINDOWS-SWITCH-COMPAT.md`)。
