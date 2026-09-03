# runtime session 分離と audio ring の Semantic Stop

## 目的

#25 runtime service と #27 AU thin head を接続する直前に、#26 契約および
audio thread 非blocking 条件との整合を確認する。

## 対象

- branch: `refactor/23-runtime-router`
- runtime: `rvc_runtime_service.py`
- AU client: `RVCRealtimeVST/src/WorkerClient_mac.cpp`
- protocol: `experiments/agent-grok-26/stream-protocol-design.md`

## 観測事実

1. 現在の最小 runtime は `serve_client()` の同一 thread で `recv`、
   `engine.process()`、`sendall` を行う。推論が heartbeat timeout 3秒を超えると
   ACK を返せない。
2. `Runtime` は同じ可変 engine instance を全 client thread へ共有する。
   `RVCStreamEngine` の `input_wav`、`sola_buffer`、`rms_buffer`、pitch cache は
   session 固有状態であり、複数 AU instance から同時に変更できる。
3. `CONFIG_UPDATE` は未実装で、現在の runtime は pitch/formant/index/rms/gate/f0
   を固定値で `process()` へ渡す。AU UI 表示と処理値が一致しない。
4. runtime は `model_id` を無視し、engine config と session の sample rate / block
   size の一致も session 受理前に確認しない。
5. 既存 client の `ready_.store(false)` 直後の `resetUnsafe()` は、audio callback が
   直前に ready=true を観測して ring 操作中でないことを保証しない。
6. 統合分岐の既存 AU は Xcode Debug build に成功した。これは legacy worker 経路の
   compile 成功であり、新 stream client の成功ではない。

## 影響

- 推論中の heartbeat timeout による再接続ループ。
- 複数 AU session 間の音声状態混線。
- UI parameter と実際の推論値の不一致。
- ring reset と audio callback の data race。

## 結果

- runtime protocol unit test: 8件 success
- AU baseline build: success
- localhost 実 port: 実行環境の bind/connect 隔離により blocked
- #27 stream client: not-implemented
- 判定: **SEMANTIC-STOP**

## Recovery / 再開条件

1. socket I/O/heartbeat thread と infer thread を分離する。
2. session ごとに独立した engine state を持たせる。重み共有は immutable core に限定する。
3. CONFIG_UPDATE と engine configuration validation を実装する。
4. ring reset を epoch/ack または generation-safe ring 交換に置き換える。
5. fake server/client で heartbeat 中の長時間推論、2 session、disconnect/reconnect、
   CONFIG_UPDATE を機械検証する。

## unknown

- GarageBand sandbox から `127.0.0.1:17865` が実際に許可されるか。
- 実機 callback の `nFrames` と runtime RTT。
- immutable model weights を session engine 間で安全に共有できる粒度。
