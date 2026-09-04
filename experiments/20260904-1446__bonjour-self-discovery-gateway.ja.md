# 実験票: Bonjour自己広告・自己発見・明示選択とlocal gateway

実施時刻: 2026-09-04 14:46 JST
対象Issue: #29、#31
branch / HEAD: `main` / `b92ef7e`
実行環境: Intel Mac、macOS実機、Python 3.12、WebUI `127.0.0.1:7865`

## 目的

単一Macで検証できるBonjour最小ケースとして、WebUIが所有するRVC runtimeを
`_rvc-realtime._tcp.local.`へ広告し、同じWebUIのbrowserで発見し、利用者が
固定LocalhostからBonjour selfへ明示選択できることを確認する。AUはBonjourを
扱わず、固定`127.0.0.1:17865`のlocal audio gatewayへ接続し続ける。

## 実装した経路

```text
AU / RSVC client
  -> 127.0.0.1:17865 local gateway
  -> 選択済みbackend
     self: 127.0.0.1:17866
     remote: Bonjour identityを選択時にresolveしたhost:port
```

- WebUI/controllerがruntime、gateway、Bonjour advertiser/browserを所有する。
- runtime backendはmacOSで`0.0.0.0:17866`をlistenする。
- TXTは`proto`、`backend`、`capacity`、非秘密runtime IDだけを広告する。
- model path、index path、tokenは広告しない。
- 選択先が消失しても別serviceへ自動failoverしない。
- 既存sessionは選択変更で途中移送せず、新規sessionから新しいtargetを使う。

## 実行コマンド

```text
.venv/bin/python -m unittest \
  tests.test_rvc_stream_protocol \
  tests.test_rvc_runtime_bonjour \
  tests.test_rvc_runtime_gateway \
  tests.test_rvc_runtime_service \
  tests.test_rvc_runtime_supervisor \
  tests.test_rsvc_loopback -v

dns-sd -B _rvc-realtime._tcp local
curl http://127.0.0.1:7865/config
curl -X POST http://127.0.0.1:7865/api/predict  # discovery / select / health
.venv/bin/python -c 'from rvc_runtime_supervisor import probe_rsvc_stream; print(probe_rsvc_stream("127.0.0.1",17865,2.0))'
```

## 観測事実

- unittest: 25/25 success。
- WebUI PID 15353が`127.0.0.1:7865`と`127.0.0.1:17865`をlisten。
- WebUI所有runtime PID 15363が`*:17866`をlisten。
- WebUI所有`dns-sd -R` PID 15364と`dns-sd -B` PID 15365を確認。
- service name: `RVC WebUI saitoomiturunoMac-Pro 26c0b2`。
- 独立`dns-sd -B`はinterface 1と7で同serviceをAddとして列挙。
- WebUI discovery APIはLocalhostとBonjour selfの2選択肢を返した。
- Bonjour selfを明示選択後、gateway targetは`127.0.0.1:17866`、状態は
  `選択中`を返した。
- `127.0.0.1:17865`へのRSVC HELLO probeは`ready`を返した。
- WebUI `/config`にBonjour dropdown、一覧更新button、選択buttonが公開された。

## 結果

機械ゲート: success。

単一Macで、自己広告、自己発見、UIと同じAPI経路での明示選択、self route、
gateway越しRSVC handshakeまで成立した。独自UDP探索は実装していない。

## Human Gate

WebUIを`127.0.0.1:7865`で起動済み。利用者がブラウザで次を確認する。

1. 「Bonjour一覧を更新」を押す。
2. `Bonjour: RVC WebUI saitoomiturunoMac-Pro 26c0b2`を選ぶ。
3. 「このruntimeを選択」を押す。
4. statusが`gateway=127.0.0.1:17865→127.0.0.1:17866`、
   `Bonjour self`、`選択中`になることを確認する。
5. 必要ならモデルを再適用し、GarageBandでoffline Bounceを再確認する。

## UNKNOWN / 外部資源

- 別Mac間の実network transport、Wi-Fi断、LAN RTT/drop。
- Windows Bonjour実装との相互運用。
- 配布app化した場合のLocal Network privacy prompt。今回はTerminal childであり、
  配布appの許可成功へ昇格しない。
- service消失・再広告のHuman Gateは未実施。

## 付随発見

旧WebUI PID 68905へSIGTERMを送った際、所有runtime PID 98002がPPID 1で残り、
旧17865 listenerを保持した。対象PIDを限定して終了し、新構成を起動した。
Bonjourとは別のshutdown signal cleanup問題としてIssue化する。
