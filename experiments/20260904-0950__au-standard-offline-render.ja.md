# AU標準offline render待機経路

## 目的

Issue #33について、独自BAKEを削除し、hostが`kAudioUnitProperty_OfflineRender`で通知した場合だけRSVC推論完了を待つ。plugin固有timeline cacheは実装しない。

## 対象

- branch: `issue-6-macos-runtime-bootstrap`
- 開始時HEAD: `c1aec63`
- 中間commit: `38d530f` / `bcd74af` / `abdd29c`
- AU: `RVCRealtime` AUv2
- runtime: WebUI所有RSVC `127.0.0.1:17865`
- Issue: #33

## 実行環境

- macOS / Intel x86_64
- Xcode macOS 15.2 SDK
- sample rate: 48000 Hz
- RVC block: 130 ms / 6240 frames
- reported latency: 12480 frames
- repository外の実モデルを稼働中runtime sessionで使用

## 入力

- 220 Hz、peak 0.1のsynthetic mono signal
- RVC 1 block
- 模擬AU callback: 512 frames。最後のcallbackだけ残数288 frames
- latency flushとして12480 framesのzero input

## 実行コマンド

```text
python3 -m unittest tests.test_rvc_stream_protocol tests.test_rvc_runtime_service tests.test_rvc_runtime_supervisor tests.test_rsvc_loopback -v
cmake --build RVCRealtime/build-macos --config Release --target RVCRealtime-au
cmake --build RVCRealtime/build-macos --config Release --target rvc-worker-smoke
RVCRealtime/build-macos/Release/rvc-worker-smoke . /usr/bin/python3 active '' 130 80 1 offline 512
```

## 観測事実

1. RSVC protocol / runtime / supervisor試験は18件成功した。
2. `RVCRealtime-au` Release buildは成功した。
3. 6240-frame blockをそのまま渡すoffline smokeは`infer_ms=1152.5`、`drop=0`、非zero出力だった。
4. 512-frame callbackへ分割したoffline smokeは次の結果だった。

```text
READY frames=6240 latency=12480 infer_ms=1238.79 output_rms=0.0182181 drops=0 blocks=1 mode=offline status="RSVC 127.0.0.1:17865 session 7"
```

5. realtime `pushInput/popOutput`経路にcondition waitは追加していない。
6. offline時だけ、一時ringの空きと対応出力をcondition variableで待つ。socketとPython推論は管理thread/runtime側に残る。
7. render mode切替時はring generationを管理threadで切り替え、次のAUDIO_INへdiscontinuity flagを付ける。runtimeはengineのtemporal bufferをzero resetしてから処理する。
8. 永続cache、timeline識別、disk I/Oは追加していない。

## 解釈

推論時間がaudio長を超えても、host側にrealtime deadlineがなければ、AU callbackの小blockをRVC blockへ集積し、出力期限で待つ経路は成立する。これはApple標準offline renderの許容範囲を使うもので、利用者操作でofflineを偽装する機能ではない。

## 結果

`partial / machine-success / human-not-tested`

- protocol・runtime・WorkerClient・AU build: success
- 実モデルoffline待機: success
- GarageBandがBounce時にoffline propertyを通知するか: not-tested
- GarageBand実音声Bounceのdrop 0 / 非zero確認: not-tested

## Recovery / 次の一手

1. 新AU componentを配備する。
2. GarageBandを完全終了して再起動する。
3. plugin上部の読取専用表示がBounce中に`OFFLINE`へ変わるか、Bounce後のperformance表示が`1 off`以上になるか確認する。
4. `OFFLINE`にもならず`0 off`のままならIssue #33の停止条件を発火し、独自cacheへ拡張しない。
5. `OFFLINE`ならBounce結果の非zero音声、drop数、DAW停止の有無を記録する。

## UNKNOWN

- GarageBandが通常Bounceで`kAudioUnitProperty_OfflineRender`を設定するか
- GarageBandが30秒/block timeout前の待機を許容するか
- host latency compensationが末尾12480 framesを常にrenderするか
- iPlug2の`void ProcessBlock`境界ではtimeoutをOSStatusとしてhostへ返せないため、DAWが失敗をどう表示するか

## 配備receipt

- 配備元: `RVCRealtime/build-macos/out/Release/RVCRealtime.component`
- 配備先: `~/Library/Audio/Plug-Ins/Components/RVCRealtime.component`
- 旧component退避先: `/private/tmp/RVCRealtime.component.pre-offline-20260904-0950`
- 最終build / 配備先binary SHA-256: `216a76f20c397b2f53f6c99b4c4cd58241bdcb4fb84c22617437051ea4d314a9`
- 旧binary SHA-256: `e570ae13856d79aa62d828f8ab83eb58a1de4b4a35fe5b274c8df243fbb4394d`
- `AudioComponentRegistrar`再起動: success
- `auval -v aufx Rvcr Rvcp`: `AU VALIDATION SUCCEEDED`
- auval render: 512 frames、64-frame slicing、mono、1-to-2 channelを含めてPASS
- offline propertyの立ち上がり回数をplugin instance内でatomicにラッチし、performance表示末尾の`N off`でBounce後にも観測可能とした。audio callbackからfile I/Oは行わない。

配備後のGarageBand完全再起動と実project BounceはHuman Gateとして未実施。
