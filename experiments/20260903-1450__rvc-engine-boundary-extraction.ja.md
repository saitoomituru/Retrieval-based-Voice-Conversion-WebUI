# RVC リアルタイムエンジン境界抽出

## 目的

Issue #24 の第一段階として、WebUI、AU/VST worker、将来の runtime service が
共有できる設定境界を固定する。GUI と audio callback の移設は次段階とする。

## 対象 Issue / branch / commit

- Issue: #24
- branch: `refactor/23-runtime-router`
- commits: `c1803a0`, `0ff6001`

## 実行環境

- macOS / Intel
- Python: `python3`
- pytest: 未導入

## 入力

- `RVCRealtimeVST/worker/rvc_worker.py`
- `realtime_gui.py`
- 既存 worker JSON 設定契約

## 観測事実

1. 既存 worker は `RVCStreamEngine` 内でパス解決、数値設定、RVC 初期化を行っていた。
2. `realtime_gui.py` は GUI、sounddevice、推論状態、SOLA、ノイズ除去を同一 `GUI` クラスに保持している。
3. AU の audio thread から Python や network を直接呼ばない方針と、GUI 側の状態をそのまま共有しない方針が必要である。
4. `RealtimeEngineConfig` を追加し、パスを絶対化し、サンプルレート・時間値を検証した。

## 解釈 / 仮説

- 設定境界を先に固定すると、runtime の所有権を WebUI 側へ戻したまま、AU/VST は薄い head として段階的に移行できる。
- 次の安全な粒度は、推論状態と block 処理を `RealtimeEngine` へ移すこと。GUI のデバイス列挙・ウィンドウ更新・stream 起動は移さない。

## 結果

- success: 設定 adapter と 3 件の標準 unittest
- not-tested: 実モデルによる end-to-end 推論、GarageBand/AU 実機、音声 callback の遅延

## Recovery / 次の一手

1. `RVCStreamEngine` の推論・SOLA 状態を共有 module へ移す。
2. worker は共有 module を呼ぶ薄い transport adapter にする。
3. `realtime_gui.py` は同じ engine を使うが、sounddevice と GUI 状態は保持する。
4. 各段階で block サイズ、sample rate、sequence、失敗状態を fixture で検証する。

## unknown

- PyTorch / TorchGate の実モデル初期化が GarageBand sandbox でどこまで許可されるかは未確認。
- runtime service の既存 WebUI API と realtime block protocol の接続点は #25/#26 で確定する。
- Gemini（#24）と Grok（#26）の外部レビュー結果は未取得。
