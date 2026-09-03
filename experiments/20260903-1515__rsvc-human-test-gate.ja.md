# RSVC 人手テスト入口

## 現在の機械ゲート

- `python3 -m unittest -v tests.test_realtime_engine_config tests.test_rvc_stream_protocol tests.test_rsvc_loopback`
- frame envelope、audio payload、little-endian、CRC、length、socketpair 往復を確認する。

## 人手テストへ進める条件

1. #25 が `127.0.0.1` の runtime health と session 起動を提供する。
2. #26 の fake server/client が同じ frame 契約で動作する。
3. AU control thread が socket I/O を所有し、audio thread に socket 呼び出しがないことをコードレビューで確認する。
4. GarageBand から `127.0.0.1:17865` に接続できることを実機で確認する。

## 人手テスト手順（未実施）

- WebUI/runtime を起動し、`127.0.0.1` の health が READY になることを確認。
- GarageBand に AU を挿入し、runtime 未起動時は dry/passthrough になることを確認。
- runtime 起動後、AU が新しい session を確立し、latency 分の zero prefill 後に変換音声へ切り替わることを確認。
- runtime 停止時、DAW audio thread が block せず dry へ戻ることを確認。
- model 切替時、旧 session の音声が新 session に混ざらないことを確認。
- 失敗時に GarageBand container 側ログと runtime 側ログの両方に session/sequence が残ることを確認。

## 状態

- 機械検証: 実施可能
- AU/localhost 実機: 未実施
- runtime service: #25 未実装
- 判定: HUMAN-TEST-BLOCKED（再開条件は上記 1〜3）
