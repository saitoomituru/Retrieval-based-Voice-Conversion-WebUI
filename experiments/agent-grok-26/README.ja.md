# experiments/agent-grok-26

実施時刻: 2026-09-03
対象 Issue: #26
branch / HEAD: `agent/grok-26-stream-protocol` / `9ec4f76d3e59e33f336081d9d676fb75172afd75`
実行環境: macOS worktree `/Users/saitoumitsuru/RVC-WebUI/.worktrees/grok-26`
結果: success（設計レビュー 0 open。プロトコルは未実装）

## 目的

AU audio head と RVC runtime 間の localhost バイナリストリーム契約を、既存 SHM worker を壊さずに文書化する。

## 入力・前提

- RVC WebUI/runtime が正本、AU は薄い audio head
- `127.0.0.1` 既定。Bonjour/LAN は #29
- audio thread に blocking I/O / Python / network / 重い allocator を置かない
- 既存 `rvc_worker.py` / `WorkerClient_{win,mac}.cpp` は凍結

## 成果物

- `stream-protocol-design.md` — 契約正本（r3、writer/reviewer 合意）
- `fixtures/` — HELLO / HELLO_NAK / AUDIO_SKIP / AUDIO_IN の hex

## 実行コマンド

設計スキルの write → review → revise ループ。本番 listen / spawn はしていない。

## 観測事実

- 現行 worker は `RVCRealtimeVST/src/WorkerClient_*.cpp` と `RVCRealtimeVST/worker/rvc_worker.py`。ユーザー指定の `worker/WorkerClient_*.cpp` は存在しない。
- README は macOS worker を stub と書くが、CMake APPLE 分岐は `WorkerClient_mac.cpp`。
- GarageBand 実変換は Issue #22 BLOCKED。本契約はそれを実装で解かない。

## 解釈 / 仮説

新契約は raw TCP `127.0.0.1:17865`、magic `0x43565352`（LE `RSVC`）。legacy SHM magic `0x50564352` とは別空間。

## Recovery / 次の一手

実装するなら PR1 pack/unpack と PR2 fake T1–T14。GarageBand localhost TCP は PR2 直後の spike。

## unknown

- GarageBand から `127.0.0.1:17865` が通るか
- 実機 `ProcessBlock` の `nFrames`
- localhost RTT 実測
