# 実験票: live-host fault-injection test — GarageBand接続中runtime 3連続SIGKILL復旧

実施時刻: 2026-09-04 10:55から10:57 JST

## 目的

GarageBandでRVCRealtime AUがRSVC runtimeへ接続中に、WebUIが所有するruntime processだけを異常終了させ、GarageBandがprocess crashせず、WebUI supervisorがboundedにruntimeを再生成し、AUが自動再接続するか確認する。

## テスト設計の位置づけ

これは「偶発的にprocessが落ちても運よく戻った」観察でも、全bugを薄く数える
網羅試験でもない。人間が実際にGarageBandを操作しAUを接続しているlive hostへ、
別の操作者であるCodexが対象runtimeのPIDと親子関係を毎回解決したうえで
`SIGKILL`を注入する **live-host fault-injection test** である。

狙いはbug総量の最小化ではなく、host巻き込みcrash、孤児process、重複listener、
無限restart、再接続不能という、遭遇頻度とは別に制作sessionを壊す影響が大きい
故障境界を選択的に踏むことにある。したがって3/3は全体品質の統計推定ではなく、
明示した故障モデルに対して同じ回復不変条件が3回成立した観測値である。

検証する不変条件は次の通り。

- DAW hostはruntimeの寿命から分離され、同一PIDで生存する
- supervisorは自分が所有するruntimeだけをboundedに再生成する
- 旧listenerや孤児processを残さず、単一listenerへ収束する
- AUは挿し直しなしで新sessionへ再接続する
- 復旧はport LISTENだけでなく、model/RMVPE loadと後続推論まで到達する

一方、この試験は全パラメータ、全host、長時間運転、リアルタイム音質を網羅しない。
それらを合格したことにはしない。

## 対象

- Issue: #20、#22、#27、#28、#31、#32
- branch: `issue-6-macos-runtime-bootstrap`
- 開始HEAD: `b19f837`
- WebUI PID: 68905
- GarageBand PID: 95008
- 初期runtime PID: 69192
- endpoint: `127.0.0.1:17865`

## 安全境界

- 各回の直前にruntime command、PPID、listen portを確認した。
- signal対象はWebUI PID 68905の子である`rvc_runtime_service.py --no-control --stream-port 17865`だけに限定した。
- GarageBand、WebUI、AudioComponentRegistrarにはsignalを送っていない。
- `killall`、port番号だけによるkill、未解決PIDへのsignalは使っていない。
- supervisor既定値は60秒内最大3 startsであるため、3回の復旧確認後に停止し、4回目のkillで意図的に上限を超えなかった。

## 初期状態

```text
WebUI      PID 68905  PPID 68271
runtime    PID 69192  PPID 68905
GarageBand PID 95008  PPID 1
GarageBand 127.0.0.1:59927 -> 127.0.0.1:17865 ESTABLISHED
runtime    127.0.0.1:17865 LISTEN
```

## 実行と観測

### 1回目

- `kill -9 69192`
- 新runtime PID: 97855、PPID 68905
- 観測時点の新runtime uptime: 16秒
- 17865 LISTEN復旧
- GarageBand client port: 62405、ESTABLISHED復旧
- cold RMVPE load: pitch 2.847秒
- 後続通常block: pitch 0.123秒、0.114秒
- GarageBand PID 95008は生存

### 2回目

- `kill -9 97855`
- 新runtime PID: 97946、PPID 68905
- 観測時点の新runtime uptime: 16秒
- 17865 LISTEN復旧
- GarageBand client port: 62486、ESTABLISHED復旧
- cold RMVPE loadを含む最初の推論完了: pitch 2.861秒
- GarageBand PID 95008は生存

### 3回目

- `kill -9 97946`
- 新runtime PID: 98002、PPID 68905
- 観測時点の新runtime uptime: 17秒
- 17865 LISTEN復旧
- GarageBand client port: 62533、ESTABLISHED復旧
- cold RMVPE load: pitch 2.606秒
- 後続通常block: pitch 0.110秒
- GarageBand PID 95008は生存

## 観測事実

- 3回のSIGKILLすべてでWebUI processは生存した。
- 3回のSIGKILLすべてでGarageBand processは同一PIDのまま生存した。
- supervisorは毎回、旧runtimeと異なるPIDの所有childを再生成した。
- AUはplugin instanceを挿し直さず、新runtimeへ異なるclient portで自動再接続した。
- 各復旧runtimeはWebUIの子であり、GarageBandの子RVC Pythonは生成されなかった。
- listener復旧だけでなく、RMVPE cold loadと後続通常block処理まで確認した。
- retry stormや複数runtime同時LISTENは観測されなかった。

## 結果

`machine-pass / human-ui-not-reviewed`

- runtime異常終了からのWebUI再生成: pass 3/3
- AU自動再接続: pass 3/3
- GarageBand process生存: pass 3/3
- WebUI process生存: pass 3/3
- GarageBand子Python不存在: pass 3/3
- 再接続後推論: pass 3/3
- GarageBand GUI応答、聴感、再生継続: human-not-reviewed
- restart上限超過時の表示と60秒後Recovery: not-tested

## 解釈

Issue #22の旧embedded worker crash構造は、現行WebUI所有runtimeでは回避できている。runtimeがSIGKILLされてもhost processは巻き込まれず、supervisorとAU reconnectが実機process/TCPレベルで成立した。

ただし、3回ともmodel/RMVPE cold loadに約数秒を要する。再接続中のGUI表示、dry fallback音、聴感上の復帰点はHuman Reviewが必要であり、本機械試験だけで合格とはしない。

## UNKNOWN / 次の一手

- kill直後から再接続までの正確な停止時間
- GUI statusがERROR/STARTING/READYを順に表示したか
- 再生中のdry fallbackと聴感復帰
- 4回目でbounded stopした場合の利用者向け表示
- 60秒window経過後の自動Recovery
- offline Bounce処理中にkillした場合のhost cancellation挙動
