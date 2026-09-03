# GarageBand BAKE drop否定検証とoffline待機経路

## 目的

Issue #14 / #33について、GarageBand実機で報告されたBAKE・bounce時のdropを記録し、REALTIMEをblockせずoffline時だけrunner出力を待つ最小修正を検証する。

## 対象

- branch: `issue-6-macos-runtime-bootstrap`
- 開始時HEAD: `59c1a0a`
- AU: `RVCRealtime`
- runtime: WebUI所有RSVC `127.0.0.1:17865`
- 関連Issue: #1 / #6 / #14 / #25 / #26 / #31 / #32 / #33

## 実行環境

- macOS / Intel x86_64
- GarageBand実機
- sample rate: 48000 Hz
- RVC block: 130 ms / 6240 frames
- model: repository外のローカルRVC model

## 入力

GarageBand project上の既存音声trackへRVCRealtime AUを挿入し、通常再生とbounceを実施した。

## 観測事実

1. AUはRSVC session 4で`READY`となり、変換音は出た。
2. GUI表示は約`1171 ms / 3035 drop`で、出力はプチプチした。
3. GarageBand bounceでもdropした。
4. 手動BAKEを押して待ってもcacheは生成されず、次回再生は軽くならなかった。
5. code上のBAKEは`GetRenderingOffline() || mForceBakeMode`の表示だけであり、`ProcessBlock`、`WorkerClient`、runtimeにoffline待機やtimeline cacheは存在しなかった。

## 解釈

- 変換経路そのものは成立したが、推論約1.2秒に対してblock 130msのためrealtime deadlineは不合格である。
- 従来Issue #14は表示UXまでを受入範囲にしており、利用者の機能目標である事前変換cacheを満たしていなかった。
- AUはホストから未供給のtrack音声を取得できないため、手動BAKEは押して待つだけではcacheを作れない。一度再生して入力を収録するか、WebUIへ既知の音声fileを渡す必要がある。

## 棄却した実装案

- host offline通知または手動BAKE時だけaudio callbackを待たせる試作は、C++ buildとRSVC smokeでは動作した。
- しかしGarageBandがoffline通知を出さない、または手動BAKEが通常再生中に残る場合、audio callbackを最大3分blockしてDAWを停止させ得る。
- repository規約のaudio thread非blocking境界にも反するため、commitせずコードから除去した。
- 採用案は、audio callbackを常に非blockingとし、事前確保ringへの収録とREADY cache読出しだけを行う方式に限定する。

## 機械検証

- 棄却案のAU build: success
- 棄却案の`rvc-worker-smoke ... 1 offline`: success
- 棄却案のsmoke結果:

```text
READY frames=6240 latency=12480 infer_ms=1126.94 output_rms=0.0203359 drops=0 blocks=1 status="RSVC 127.0.0.1:17865 session 5"
```

- protocol / engine config / runtime session / supervisor unittest: 17/17 success
- `git diff --check`: success

## 結果

`partial`

- offline出力待機のC++/RSVC経路: 動作したが安全境界違反により棄却
- GarageBandへの再配備とbounce再試験: not-tested
- timeline変換cache: not-implemented

## Recovery / 次の一手

1. Issue #33の`ARMED -> CAPTURING -> PROCESSING -> READY / STALE` cache state machineを別commitで実装する。
2. AU audio callbackは事前確保ringへの入力収録とREADY cache読出しだけを行う。
3. runtime/background threadが推論とcache commitを担当する。
4. 新AUを配備し、同一timelineの2回目再生とbounceでdrop 0を確認する。

## unknown

- GarageBandがbounce時に`kAudioUnitProperty_OfflineRender`を通知するか。
- GarageBandが1 block最大3分のoffline待機を許容するか。
- 手動BAKE中の通常再生をhostがoffline相当として扱えるか。
- seek、loop、project reopenを跨ぐtimeline sample positionの安定性。
