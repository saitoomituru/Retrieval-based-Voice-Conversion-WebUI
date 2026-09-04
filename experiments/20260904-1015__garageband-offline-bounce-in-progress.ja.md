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

## 追記: 単トラックSolo Bounce完了

観測時刻: 2026-09-04 10:19 JST

### 人間による報告

- GarageBand標準Bounceを単トラックSolo modeで完走した。
- 生成音声の視聴テストは合格した。
- 次は設定を初期化し、オケを含むBounceを試験する。

これは利用者によるHuman Reviewであり、agentによる聴感判定ではない。

### 終了後の機械観測

- runtime PID 69192は生存し、CPU 0.0%、state Sへ戻った。
- WebUI PID 68905とGarageBand PID 93090も生存していた。
- 17865はruntimeのLISTENだけが残り、GarageBandとのESTABLISHED sessionは正常に消えた。
- runtime logは68行/4476 bytesから70行/4603 bytesへ増え、最終更新は10:16:29 JSTだった。
- 追加された末尾blockは`features=0.552s, index=0.000s, pitch=0.116s, model=0.467s`を記録した。
- process crash、接続残留、runtime再起動は観測されなかった。

### 更新判定

`solo-bounce-human-pass / session-cleanly-closed / full-mix-not-tested`

- 単トラックSolo Bounce聴感: human-pass
- Bounce完走後のruntime生存: observed
- RSVC session終了: observed
- オケ付きBounce: not-tested
- offline通知回数、drop表示: not-reported
- timeline alignment、末尾欠損: not-reported

## 最終追記: 設定初期化後のオケ付きMix Bounce合格

観測時刻: 2026-09-04 10:27から10:45 JST

### 人間による報告

- 設定を初期化し、オケを含むGarageBand標準Bounceを実行した。
- オケ付きmixの生成音声は完全に聴取可能で、視聴テスト合格だった。
- realtime再生は引き続きプチプチが発生し不合格だが、標準Bounceでは問題を回避できた。

### 画像から観測した事実

利用者提供のGarageBand Bounce中画像では次を確認した。

- plugin render mode: `OFFLINE`
- engine status: `READY`
- performance: `1183 ms / 0 drop / 1 off`
- RSVC endpoint/session: `127.0.0.1:17865 session 10`
- model: `zundamon_40k_v2_20260902.pth`
- index: 空欄
- GarageBandは複数のオケtrackを含むprojectをBounce中

これにより、GarageBandが標準offline propertyを通知したこと、offline経路で約1.18秒の推論完了を待ち、drop 0で処理したことが視覚的に確認された。

### 終了後の機械観測

- 設定初期化に伴いGarageBandはPID 95008として10:19:52 JSTに再起動されていた。
- WebUI PID 68905、runtime PID 69192は継続生存した。
- 10:45:58時点のruntimeはCPU 0.0%、state S。
- GarageBand PID 95008とruntime PID 69192のRSVC接続はESTABLISHED。pluginがREADYのまま開かれている状態と整合する。
- GarageBand PID 95008の子processは観測されず、RVC PythonはWebUIの子のまま。
- runtime logは70行/4603 bytesから98行/6320 bytesへ増加し、最終更新は10:38:44 JST。
- この試験区間の末尾にはRMVPE loadが1回あり、その後はpitch約0.10から0.12秒の処理へ戻った。設定初期化後sessionのcold loadと整合し、反復reloadの証拠ではない。

### 最終判定

`offline-bounce-human-pass / realtime-fail`

- GarageBand標準offline property通知: pass (`OFFLINE`, `1 off`)
- offline処理: pass (`1183 ms`, `0 drop`)
- 単トラックSolo Bounce聴感: human-pass
- オケ付きMix Bounce聴感: human-pass
- WebUI所有runtime / GarageBand子Python不存在: pass
- realtime playback: fail（CPU推論が130 ms block deadlineを超過しプチプチ）
- Logic Pro / Windows / 別Mac LAN: not-tested

Issue #33の標準offline render目標は達成した。realtime性能は同Issueの非目標どおり別Issueで追跡する。
