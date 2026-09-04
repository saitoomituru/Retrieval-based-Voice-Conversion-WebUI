# 実験票: GarageBand標準Bounce実行中の非侵襲観測

実施時刻: 2026-09-04 10:15 JST

## 目的

Issue #33のGarageBand標準offline render Human Gate実行中に、処理を停止・sample・再起動せず、process所有関係、RSVC接続、CPU負荷、runtime logを読み取り観測する。

## 対象

- Issue: #22、#31、#33
- branch: `issue-6-macos-runtime-bootstrap`
- HEAD: `ddc5de1`
- GarageBand: PID 93090
- WebUI: PID 68905
- RVC runtime: PID 69192

## 実行コマンド

```text
ps -axo pid,ppid,%cpu,%mem,etime,state,command
lsof -nP -iTCP:17865
lsof -nP -p 69192
stat / wc -l / tail logs/rvc-runtime-service.log
```

`sample`、debugger attach、signal送信、component再配備、process再起動は行っていない。

## 観測事実

### process所有関係

10:15:11と10:15:33 JSTの二回観測で、次を確認した。

- GarageBand PID 93090、PPID 1、CPU 7.1%から10.3%、state S
- WebUI PID 68905、CPU 0.4%
- RVC runtime PID 69192、PPID 68905、CPU 98.5%から99.2%、state R
- runtime commandは`rvc_runtime_service.py --no-control --stream-port 17865 --engine-config .../TEMP/rvc-runtime-engine.json`
- GarageBandを親とするRVC Python childは観測されなかった
- runtimeはGarageBandではなくWebUIの子processであり、Issue #22の旧embedded worker所有構造ではない

### RSVC接続

```text
Python    69192  TCP 127.0.0.1:17865 (LISTEN)
Python    69192  TCP 127.0.0.1:17865->127.0.0.1:57656 (ESTABLISHED)
GarageBan 93090  TCP 127.0.0.1:57656->127.0.0.1:17865 (ESTABLISHED)
```

GarageBand processからWebUI所有runtimeへ、loopback RSVC sessionが実際に接続されたままBounceが進行している。

### runtime log

- path: `logs/rvc-runtime-service.log`
- 10:15:33時点: 4476 bytes、68 lines
- 最終更新: 10:14:28 JST
- runtimeのstdout/stderr file descriptor 1/2はいずれも同logへwrite open
- logにはRVC blockごとの`features/index/pitch/model`処理時間と、`インデックス検索に失敗したか無効です`が記録されている
- 観測範囲の通常block例では、features約0.52から0.71秒、pitch約0.10から0.16秒、model約0.42から0.59秒
- 同一log全体には`RMVPEモデルを読み込み中`とpitch 0.9から3.8秒の行も複数ある。ただしlog行にtimestamp/session/sequenceがないため、全てを今回のBounceだけへ帰属できない

## 解釈 / 仮説

- runtimeが約99% CPUで連続稼働し、GarageBandとのTCP sessionがESTABLISHEDのため、遅いBounceはRVC演算待ちと整合する。
- 現在のprocess treeは、GarageBand sandbox内でPython/PyTorchをspawnしていたIssue #22の障害構造を回避している。
- logにprotocol flag、session ID、sequence、offline/real-time mode、dropが無いため、このfileだけでは`kAudioUnitProperty_OfflineRender`通知を証明できない。Bounce終了後のplugin表示`N off`がHuman Gateである。
- `RMVPEモデルを読み込み中`の反復はcache失効の可能性があるが、timestamp不足のため現時点では仮説に留める。

## 結果

`in-progress / process-and-transport-observed / audio-not-yet-reviewed`

- GarageBand → RSVC接続: observed
- WebUI所有runtime: observed
- GarageBand子Python不存在: observed at two snapshots
- runtime high CPU processing: observed
- offline property: not yet confirmed
- Bounce完了: not yet confirmed
- drop 0 / 出力音声: not yet reviewed

## 次の一手

1. Bounceを人間操作で完走させる。
2. plugin performance表示の`N off`、drop、statusを確認する。
3. 生成音声の非zero、プチプチ、長さ、末尾欠損をHuman Reviewする。
4. 完了後のlog size/末尾とTCP close/session状態を再採取する。
5. `0 off`ならIssue #33停止条件を発火する。

## UNKNOWN

- GarageBandがoffline propertyを通知したか
- Bounce中の全AUDIO_IN sequenceがAUDIO_OUTへ対応したか
- logのstdout buffering量と未flush行
- RMVPE再読込が今回session内で発生したか
- 生成音声の品質、drop、timeline alignment
