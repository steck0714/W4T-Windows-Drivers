# hayato41200/Windows-for-Switch-Drivers の kits — 再配布用の写し(Erista / HAC-001)

上流: https://github.com/hayato41200/Windows-for-Switch-Drivers  固定コミット `28ff74c30fad3faf4b329335881d9854ca2064f5`(取得 2026-11-20)  ライセンス: **GPL-2.0**(`LICENSE` 同梱)。
`kits/` を上流のまま無改変で置いた(スクリプトと SD 用の `SDLOADAA64*.EFI`)。**CI は `drivers` `agent` `fwfetch` `test-signing` だけを入れ替えるので、このフォルダは上書きされない。**

- `.EFI` の対応ソース: `reference/hayato41200/Windows-for-Switch-Drivers/drivers/Tegra210Sdhc/loader/sd-acpi-chainloader.zig`(同コミット)。GPL-2.0 のバイナリとして配るときは、このリポジトリの該当コミットを一緒に提供する(`COPYING.md`)。
- 上流の `.cer`(`SwitchWOA-Test.cer`)は**入れていない**。この `build/HAC-001/` の証明書は CI が作る `SwitchWOA-Test.cer` だけ(信頼する証明書を混ぜない)。上流の kits のスクリプトが `.cer` を探す場合は、こちらの `.cer` を同じ場所に置く。
- 実機での動作はこちらでは未確認。ファームウェア(NVIDIA / Broadcom)は含まれない。
- 全体のスナップショットは `reference/hayato41200/`。
