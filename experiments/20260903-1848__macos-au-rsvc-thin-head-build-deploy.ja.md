# macOS AU RSVC thin head ビルド・配備確認

## 目的

Issue #23 / #25 / #27 の localhost runtime 分離案について、macOS AU を Python 内包型から RSVC thin head へ切り替え、GarageBand 再起動前までの機械検証と配備を完了する。

## 対象 Issue / commit / branch

- Issue: #23, #25, #27, #31
- branch: `refactor/23-runtime-router`
- 実装 commit:
  - `f9911a3 runtimeの推論threadとsession状態を分離`
  - `6ea6b57 macOS AUをlocalhost RSVC thin headへ移行`
- 検証時 HEAD: `6ea6b579ffa1399c52bbe6709e8ad0602e52a9bb`
- 担当: Codex
- 設計レビュー: `mac_thin_head_review`

## 実行環境

- macOS / Apple Audio Unit v2
- CMake Xcode generator / Debug
- iPlug2: `/Users/saitoumitsuru/RVC-WebUI-gui/RVCRealtimeVST/third_party/iPlug2`
- RSVC control: `127.0.0.1:17864`
- RSVC stream: `127.0.0.1:17865`

## 入力

- runtime 既定 engine: passthrough
- smoke test: 48 kHz、block 480 frames、2 blocks
- AU 構成: `aufx/Rvcr/Rvcp`

## 実行コマンド

```sh
python3 -W error::ResourceWarning -m unittest -v \
  tests.test_rvc_runtime_service \
  tests.test_rvc_stream_protocol \
  tests.test_rsvc_loopback \
  tests.test_realtime_engine_config

cmake -S RVCRealtimeVST -B RVCRealtimeVST/build-macos -G Xcode \
  -DIPLUG2_DIR=/Users/saitoumitsuru/RVC-WebUI-gui/RVCRealtimeVST/third_party/iPlug2 \
  -DRVC_BUILD_SMOKE_TEST=ON \
  -DRVC_MAC_LEGACY_EMBEDDED_WORKER=OFF

cmake --build RVCRealtimeVST/build-macos --target RVCRealtime-au --config Debug
cmake --build RVCRealtimeVST/build-macos --target rvc-worker-smoke --config Debug

python3 rvc_runtime_service.py --control-port 17864 --stream-port 17865
RVCRealtimeVST/build-macos/Debug/rvc-worker-smoke ignored ignored active "" 20 10 2

/usr/bin/codesign --verify --deep --strict --verbose=2 \
  /Users/saitoumitsuru/Library/Audio/Plug-Ins/Components/RVCRealtime.component

/usr/bin/auval -v aufx Rvcr Rvcp
```

## 観測事実

- Python unit test は 11件すべて成功した。
- block 中の推論に対して heartbeat 応答を継続できた。
- 2接続が同一可変 engine 状態を共有しないことを確認した。
- `RVCRealtime-au` と `rvc-worker-smoke` はともにビルド成功した。
- localhost TCP end-to-end smoke は次を返した。

```text
READY frames=960 latency=1920 infer_ms=0.385537 output_rms=0.0704574 drops=0 blocks=2 status="RSVC 127.0.0.1:17865 session 1"
```

- runtime停止後、ports 17864 / 17865 に LISTEN process は残っていない。
- 新しいAUを次へ配備した。

```text
/Users/saitoumitsuru/Library/Audio/Plug-Ins/Components/RVCRealtime.component
```

- 旧AUは次へ退避してあり、回収可能である。

```text
/Users/saitoumitsuru/Library/Audio/Plug-Ins/Components/RVCRealtime.component.backup-before-rsvc-20260903
```

- 新AUの実行binaryは build output と installed bundle で一致した。

```text
SHA-256 60b24af6d37627ef8ca410ba5efb440f0f929afade1b5666b4b286bf9adda390
```

- installed bundle の `codesign --verify --deep --strict` は成功した。
- 新AUのResourcesには `Roboto-Regular.ttf` のみ存在し、旧 `rvc_worker.py` は含まれない。
- `auval` は channel 1-1 / 1-2 / 2-2、render、custom UI を通過し、`AU VALIDATION SUCCEEDED` となった。
- AudioComponentRegistrar は再起動済みである。

## 解釈 / 仮説

- AU audio thread はring bufferへの読み書きだけを行い、localhost接続・RSVC frame処理・heartbeatは管理thread側へ隔離できた。
- AppleのAU境界内にPython/RVC runtimeを所有させず、OSが許可したlocalhost workerへheadをroutingする構成は、少なくともbuild、RSVC実通信、AU validationの各境界で成立している。
- OSのlocalhost許可前にtimeoutし、許可後に同一serviceがREADYを返したため、OS検疫・許可境界を迂回せず利用する設計の否定検証にもなった。

## 結果

`success` — 手順4「AU rebuild / redeploy」まで完了。

GarageBandを再起動しての実音確認は手順5なので、この記録では実施・成功を主張しない。

## Recovery / 次の一手

1. 実RVC用 `--engine-config` を指定して localhost runtime を起動する。
2. GarageBandを再起動する。
3. AUを挿入し、音声routing、dropout、latency、runtime停止・再起動時の復帰を人間が確認する。
4. 問題時は現AUを外し、退避済み `.backup-before-rsvc-20260903` を元名へ戻す。

## unknown / 未試験

- runtime既定起動はpassthroughであり、実RVC model/indexを使った実音変換は未試験。
- AUのpitch、formant、index rate、RMS mix、gate、F0 methodをruntimeへ反映する `CONFIG_UPDATE` は未実装。
- `SESSION_ACCEPT` が返すlatencyとAU hostへの動的latency通知の一致は未検証。
- GarageBand再起動後のAU discovery、実音、preset、長時間dropoutは未試験。
- Bonjour / LAN worker discoveryは今回のlocalhost必須条件外であり未実装。
