# WebUI既定とAU sessionモデル選択のHuman Gate準備

## 目的

Issue #38について、WebUI既定モデルとAU明示モデルを分離し、remote runtimeにも通用するopaque ID契約をHuman Gate直前まで実装する。

## 対象

- branch: `main`
- commits: `4b6fc42`, `966ef87`, `703a556`
- runtime: `rvc_runtime_service.py`, `rvc_runtime_control.py`, `webui.py`
- AU: `RVCRealtime/src/`

## 実行環境

- Intel Mac
- GarageBand / AUv2
- Python 3.12 virtual environment
- localhost gateway `127.0.0.1:17865`
- local backend `0.0.0.0:17866`

## 入力

- WebUIで既に選択されていた `deltamon_singing_40k_v2_20260901.pth` を既定値として維持
- `assets/weights` 内の12モデルをruntime catalogへ登録
- モデル本体と学習データはGit管理対象外

## 実行コマンド

```text
.venv/bin/python -m unittest tests.test_rvc_runtime_service tests.test_rvc_runtime_supervisor tests.test_rvc_runtime_control tests.test_rvc_runtime_gateway
cmake --build RVCRealtime/build-macos --config Release --target RVCRealtime-au
auval -v aufx Rvcr Rvcp
```

WebUIの `rvc_runtime_configure` APIを既存deltamon選択のまま一度実行し、旧単一model configをcatalog形式へ明示更新した。

## 観測事実

- Python統合テスト25件は全件成功した。
- opaque IDの単体追加テスト12件は全件成功した。
- AU Release buildは `BUILD SUCCEEDED`。
- 配備前componentは `/private/tmp/RVCRealtime.component.pre-model-selector` へ退避した。
- 更新componentをuser Audio Unit directoryへ配備し、`auval` は `AU VALIDATION SUCCEEDED`。
- RSVC control応答は `active` と12件の `rvc-<hash>` ID、表示名を返した。model実パスはcatalog広告へ出していない。
- `active` のWebUI既定はdeltamonである。
- 既往Human GateではBonjour自己発見後の実変換、`-12 st`、標準offline Bounceが成功済み。

## 解釈

- WebUI変更はruntimeのdefault modelを更新する。
- AU明示選択はAU stateへopaque IDを保存し、管理threadが再接続して`SESSION_OPEN`へ渡すため、そのsessionではWebUI defaultより優先される。
- runtime切替時はいったん`active`へ戻すため、別runtime発行IDの誤用を避ける。
- audio callbackにはmodel発見、HTTP、Bonjour、filesystem、Python待ちを追加していない。

## 結果

`success`。GarageBand上でWebUI既定とAU明示モデルを切り替え、モデル選択、設定スロット保存・復元、表示名再解決をHuman Gateで確認した。ずんだもん設定は変換音と表示名の双方で復元した。

## Human Gate

1. GarageBandを完全終了して再起動する。
2. RVC Realtimeを開き、`RUNTIME / SCAN / SELECT`でLocalhostを選ぶ。
3. MODEL行右端の`▾`から `zundamon_40k_v2_20260902.pth` を選ぶ。
4. Engineが自動的に再接続してREADYへ戻ることを確認する。
5. WebUI既定がdeltamonのまま、GarageBand標準offline Bounceでzundamon音色になることを聴取する。
6. MODELから `WebUI default: deltamon...` を選び、再Bounceでdeltamonへ戻ることを聴取する。

## unknown / 非保証

- GarageBandで新MODEL menuの表示・操作、設定スロット復元はHuman Gate合格。
- 1 runtimeへ複数clientが接続した場合の排他、公平性、資源予約は保証外。
- 別Mac間BonjourとWindows互換は実機資源待ち。

## Recovery

新component固有の起動障害があればGarageBandを終了し、退避済み `/private/tmp/RVCRealtime.component.pre-model-selector` をuser Audio Unit directoryへ戻せる。

## 保存スロット表示regressionと修正

Human Gate開始時、AUでずんだもんを選んでGarageBandの設定スロットへ保存し、復元するとMODEL欄が表示名ではなく`rvc-d5d27b9ef69f373b`になった。変換用ID自体は正しく保存され、更新前の設定ファイルも有効だった。

原因は、presetへ保存すべきstable opaque IDと、人間へ見せるruntime由来の表示名を同じUI fieldで扱い、state復元後のID→表示名解決を行っていなかったことである。保存形式を表示名へ戻すとrenameやremote runtimeで壊れるため、opaque IDの保存は維持した。

修正では、`UnserializeState`と`OnUIOpen`がatomicな更新要求だけを立て、UI idle threadがlocalhost controllerのmodel catalogから表示名とindex名を再解決する。state復元処理とaudio callbackにはnetwork I/Oを置かない。AU Release build、全Python回帰43件は再度成功した。GarageBandでのスロット保存→復元表示も再Human Gateに合格した。
