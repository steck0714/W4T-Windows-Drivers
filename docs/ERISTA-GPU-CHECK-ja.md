# Erista-GPU.zip（TegraGpuCore.lib）は GPU ドライバとして足りているか — 2026-11-19 確認

**結論: 足りない。これは「ドライバ」ではなく、ドライバの中身になる静的ライブラリ。** `.sys` にはならず、単体ではインストールできない。
確認は `.lib` のシンボル解析（ホスト上、実行なし）とリポジトリの README / ワークフローの突き合わせだけ。実機・Windows では未確認。

## 中身（実測）
- ARM64 の COFF オブジェクト 54 個、外部に見える定義 362 個。
- 外部への依存は ntoskrnl の関数だけ（Ex/Ke/Mm/Ps/Zw/Rtl、memcpy/memset）。**dxgkrnl への依存（DxgkInitialize など）も `DriverEntry` も無い。**
- 入っているもの
  - 描画向け DDI 24 本: AddDevice / StartDevice / StopDevice / RemoveDevice、QueryAdapterInfo、Create・Destroy の Device / Process / Context / Allocation、Open・CloseAllocation、BuildPagingBuffer、SubmitCommandVirtual、PreemptCommand、QueryCurrentFence、ResetFromTimeout / RestartFromTimeout、Get・SetRootPageTable、DpcRoutine。
  - ハード制御: ACR / PMU / GR / フロントエンド / MMU（GM20B）/ クロック / 電源 / 割り込み / フォールト。

## 足りないもの
| 足りないもの | どこにあるか・どうするか |
|---|---|
| `DriverEntry` と DDI 登録 | `drivers/t210-icosa/drivers/TegraGpu/windows/render_miniport.c` 側。ただし README は「probe（System クラスの KMDF テスト用）で、表示ミニポートでも Vulkan ICD でもない」と明記 |
| 割り込み DDI（InterruptRoutine） | ライブラリには無い（DpcRoutine のみ）。ラッパー側 |
| 画面表示（VidPn、Present、ホットプラグ） | ライブラリには無い。現状は表示専用の `SwitchDisplay`（winx-display-kit 系）が担当 |
| スリープ（SetPowerState） | ライブラリには無い |
| ユーザーモード側（Vulkan ICD / D3D UMD） | mesa-switch 側。このライブラリには無い |
| NVIDIA ファーム（GR 3 ファイル + ACR 6 ファイル） | **再配布不可**。CI は `FW_BUNDLE_URL` の自前 zip から取り込む。無いと**このライブラリだけ**が作られる（今回の zip がその状態） |

## 次にやること
1. 自分の SD から作ったファーム zip を `FW_BUNDLE_URL` に設定して `build-erista-gpu.yml`（`build-all-drivers` から呼ばれる）を再実行 → probe ドライバが署名される。
2. それでも「GPU ドライバ」ではなく動作確認用。画面は表示専用ドライバのまま。完成には表示ミニポート + UMD が別途要る。
