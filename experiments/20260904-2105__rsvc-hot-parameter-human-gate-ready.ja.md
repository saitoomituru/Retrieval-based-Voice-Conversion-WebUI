# RSVC hot parameter実装・GarageBand Human Gate準備

- 対象 Issue: #37
- 対象 branch / HEAD: `main` / `d8da037`時点の作業継続
- 実行環境: Intel Mac Pro、macOS 15.7.7、Python 3.12.7、Xcode 16.2
- 実行者: Codex
- 日時: 2026-09-04 20:34–21:05 JST
- 結果: `machine-pass / deployed / human-test-pending`

## 目的

AU GUIのpitch / formant / index rate / RMS mix / gate / F0 methodを見かけだけの値にせず、RSVC session-local hot parameterとして実RVC推論へ渡す。model/indexの実path所有はWebUIへ戻し、AU thin headからの任意filesystem選択を廃止する。

## 実装

- RSVC v1設計済みの固定24 bytes `CONFIG_UPDATE` / `CONFIG_ACK`を実装した。
- AU audio callbackはatomic parameterを更新するだけで、管理threadが初回session accept後と変更時に送信する。
- runtimeは範囲と単調増加sequenceを検証し、次に受信する`AUDIO_IN`へparameter snapshotを結び付ける。
- configはsessionごとに独立し、global engine設定へ書き戻さない。
- block / crossfade / context / sample rateは従来どおりsession再作成とした。
- AUのMODEL/INDEXは`MODEL (WEBUI)` / `INDEX (WEBUI)`のread-only表示へ変更し、controllerからbasenameだけを取得する。

## 機械検証

- repository Python test: 41/41 success
- protocol/service/control/lifecycleの局所試験: success
- APP / AU Release build: success
- `git diff --check`: success
- `codesign --verify --deep --strict`: success
- `auval -v aufx Rvcr Rvcp`: `AU VALIDATION SUCCEEDED`
- C++ thin head → initial `CONFIG_ACK` → localhost gateway → 実RVC engine offline 1 block:
  - frames 6240
  - latency 12480
  - infer 1255.02 ms
  - output RMS 0.0177178
  - drop 0
  - RSVC session 2

## 配備

- build binary SHA-256: `b68caca992008eb7c57029819a707e91103ffe8945cbcf94faf38a25d6027af3`
- 配備binary SHA-256: 同一
- 旧component退避先: `/private/tmp/RVCRealtime.component.pre-config-update-20260904-2055`
- WebUI PID 49353、実RVC runner PID 49357で起動済み
- control APIはmodel `deltamon_singing_40k_v2_20260901.pth`を返却

## 次のHuman Gate

GarageBandは旧binaryをロード中の可能性があるため、再起動後に行う。

1. AU画面のMODEL/INDEXがread-onlyのWebUI所有表示になったことを確認する。
2. 同じ音声、同じmodel、同じ他parameterでpitchを`0 st`にし、標準offline bounceする。
3. pitchだけを`12 st`へ変更し、もう一度標準offline bounceする。
4. 2ファイルを試聴し、両方にRVC変換があり、意図したpitch差があることを確認する。
5. realtimeのプチプチは#35既知障害なので、本ゲートの不合格理由にしない。

## UNKNOWN

- 実モデルでのpitch A/B可聴差はHuman Gate待ち。
- formantの可聴差とFCPE/PM availabilityはこの最小ゲート外。
- Windows legacy SHM adapterはsourceを変更していないが、Windows実機資源がないため未試験。
