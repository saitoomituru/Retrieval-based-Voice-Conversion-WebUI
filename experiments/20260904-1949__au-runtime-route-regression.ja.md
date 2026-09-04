# AU runtime選択後のaudio route回帰修正

対象Issue: #29（関連: #25 #27 #31 #35 #36）

対象branch / HEAD: `main` / `c6553bf`

実行環境:

- macOS 15.7.7 / Intel x86_64
- GarageBand 10.4.14
- WebUI `127.0.0.1:7865`
- AU固定gateway `127.0.0.1:17865`
- runtime backend `*:17866`

## 目的

AU内のruntime選択GUIは動くが、Localhost / Bonjour selfを選択しても
変換音にならないというHuman Test結果を、表示状態、gateway route、RSVC
session、engine設定に分けて再現・修正する。

## 入力

- Human Test: GarageBand上のAU GUI表示・選択操作は合格
- Human Test: 選択後のrealtime音声、offline bounceとも声質変換を確認できず
- model: `assets/weights/deltamon_singing_40k_v2_20260901.pth`
  （gitignore対象。repositoryへ含めない）
- index: なし

## 観測事実

1. 調査開始時のruntime PID 16992のcommand lineには`--engine-config`がなく、
   WebUI supervisorの表示規約上も`engine=passthrough`だった。
2. 「選択音色をAUへ適用」で生成していた設定は`TEMP/`配下だったが、WebUIは
   起動時に`TEMP/`を消去する。このため明示適用したmodel選択がWebUI再起動後に
   失われ、passthroughへ戻った。
3. `RsvcGateway.select()`はtargetだけを更新し、確立済みTCP sessionを維持していた。
   AUは固定gatewayとの既存sessionを使い続けるため、GUIの選択表示と実audio routeが
   分離していた。
4. Bonjourで列挙したremote表示は今回、同一Mac・同一backend `127.0.0.1:17866`
   へのself routeである。Localhostとの切替で声質が変わらないこと自体は正常であり、
   route世代・TCP tuple・session再確立で判定する必要がある。
5. WebUIへSIGTERMを送ると所有runner PID 16992が再び孤児化した。これは既知の#36を
   再現したものであり、今回のaudio route修正と混ぜない。

## 変更

- `RsvcGateway.select()`でroute generationを進め、確立済みclient/backend socketを
  shutdownする。AUの管理threadが固定localhost gatewayへ再接続し、新targetで
  RSVC sessionを作り直す。audio callbackにはnetwork/blocking処理を追加しない。
- active session数とroute generationをsnapshotへ追加する。
- 明示適用されたengine設定を、起動時に消える`TEMP/`からgitignore済みの
  `logs/rvc-runtime-engine.json`へ移す。モデルを勝手に選ぶのではなく、最後の人間の
  明示選択だけを再利用する。

## 機械検証

```text
.venv/bin/python -m unittest discover -s tests -p 'test_rvc_*.py'
.............................
Ran 29 tests in 5.213s
OK
```

追加testは、旧sessionが選択時にEOFとなり、新sessionだけが新backendへ届くこと、
永続engine設定が初回spawn commandへ`--engine-config`として入ることを固定する。

実RVC engineを設定したC++ thin head 1 block変換:

```text
READY frames=6240 latency=12480 infer_ms=1128.54 output_rms=0.0183447 drops=0 blocks=1 status="RSVC 127.0.0.1:17865 session 4"
```

入力は0.1振幅の220 Hz sine（RMS約0.0707）であり、出力RMS 0.0183447は
passthroughの同値コピーではない。これは接続・推論成立の機械証拠であり、声質の
人間聴感合格を代替しない。

## live-host route切替

GarageBand PID 18142が再生可能な状態で、control APIから明示的にrouteを切り替えた。

| 状態 | route generation | GarageBand → gateway | gateway → backend |
|---|---:|---|---|
| Localhost（初期） | 0 | `55930 → 17865` | `55931 → 17866` |
| Bonjour self | 1 | `56031 → 17865` | `56032 → 17866` |
| Localhost（再選択） | 2 | `56057 → 17865` | `56058 → 17866` |

各切替後に`active_sessions=1`へ戻り、GarageBand PID 18142、WebUI PID 35708、
RVC engine runner PID 35732は生存した。runner command lineには
`--engine-config /Users/saitoumitsuru/RVC-WebUI/logs/rvc-runtime-engine.json`がある。

## 結果

`implementation-success / machine-gate-pass / human-audio-retest-required`

GUI表示だけ変わり実audio sessionが残る回帰と、再起動でpassthroughへ戻る回帰は修正した。
GarageBandでの変換音再生とoffline bounceは、修正版プロセスに対するHuman Gateを残す。

## unknown / 次の一手

- 別Mac runtimeは物資未提供のため未試験。Bonjour selfは切替機構の最小試験であり、
  別machine transport、LAN RTT/drop、異なるremote modelの声質差を証明しない。
- 同一backendのLocalhost / Bonjour selfでは声質差を合否条件にしない。
- #35のIntel CPU realtime deadline超過は未解決。offline bounceは別途合格済み。
- #36のSIGTERM orphan回収は未解決。
- Human Gate: GarageBandでENGINE ON、Localhost選択後の変換音、Bonjour self再選択後の
  session復帰、offline bounceを聴感確認する。
