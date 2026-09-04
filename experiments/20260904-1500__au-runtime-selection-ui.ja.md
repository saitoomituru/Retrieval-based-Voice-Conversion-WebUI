# 実験票: AUv2 runtime engine検出・切替UI

実施時刻: 2026-09-04 15:00 JST
対象Issue: #29、#31
branch / HEAD: `main` / `66400d7`
実行環境: Intel Mac、macOS、Xcode 16.2、GarageBand

## 目的

WebUIだけでなくGarageBand内のAUv2画面からも、WebUI/controllerが検出した
runtime engine一覧を更新・選択できるようにする。

## 責務境界

AUがBonjour browseを直接実行する仕様には戻さない。WebUI/controllerが
Bonjour広告・探索・resolveとgateway選択を所有し、AU GUIはlocalhost control
planeへ一覧取得・選択要求を送る。

```text
AU GUI -- UI thread / HTTP 127.0.0.1:17864 --> WebUI runtime control
AU audio -- audio callback / RSVC 127.0.0.1:17865 --> local gateway
WebUI controller -- Bonjour --> runtime backend候補
```

- network処理は利用者が`SCAN / SELECT`を押したUI threadだけで実行する。
- audio callbackへHTTP、Bonjour、filesystem、重いallocatorを追加しない。
- AU clientは350 msのsend/receive timeoutを持つ。
- WebUI停止時は`RUNTIME CONTROL OFFLINE`をAU内に表示する。
- 選択は新規RSVC sessionから反映し、再生中sessionを途中で別engineへ移送しない。

## UI変更

macOS thin-head版では、AU内で意味を失った`RVC ROOT`と`PYTHON`の2行を、
`RUNTIME`パネルへ置き換えた。`SCAN / SELECT ▾`を押すとWebUI controllerの
検出一覧をpopup表示し、選択後は選択名とcontrol経路をパネル内に表示する。
Windowsとlegacy embedded-workerの既存path UIは変更しない。

## control contract

- `GET 127.0.0.1:17864/v1/runtimes.txt`: percent encodeされた選択中runtimeと候補
- `POST 127.0.0.1:17864/v1/select-text`: 選択名をpercent encodeして送信
- JSON APIはWebUI用として従来どおり併存
- control portはloopbackだけをlisten

## 機械検証

- Python control/Bonjour/gateway test: 10/10 success。
- RSVC全体suite: 27/27 success（control API追加時）。
- `RVCRealtime-au` Release build: success。
- `RVCRealtime-app` Release build: success。
- 配備前componentは`/tmp/rvc-au-backup.IwC8B0/RVCRealtime.component`へbackup。
- build版と配備版binary SHA-256一致:
  `e168c33b141525e69090076ef088c9fd4fa6236f16e5c520f1d4d633cc8708b4`
- `codesign --verify --deep --strict`: success。
- `auval -v aufx Rvcr Rvcp`: `AU VALIDATION SUCCEEDED`。
- custom Cocoa view `RVCRealtime_View`: PASS。
- WebUI再起動後、`127.0.0.1:17864/v1/runtimes.txt`はLocalhostとBonjour selfを返した。

## 失敗とRecovery

最初のbuildは、build中のCMake再生成で新`.cpp`がXcode projectへ追加されたため、
その回の事前dependency graphにobjectが入らずlink errorになった。project生成後に
再buildし、`RuntimeControlClient_mac.cpp`がAU/APP双方でcompileされることを確認した。

sandbox内の再buildはXcode DerivedDataへの書込拒否でも停止した。許可環境で同じ
buildを再実行して成功した。source errorとして扱っていない。

## Human Gate

新componentは配備済みだが、観測時のGarageBand processは旧binaryをロード済み。
GarageBandを再起動して次を確認する。

1. RVC Realtime AUを開く。
2. 上部に`RUNTIME`と`SCAN / SELECT ▾`が表示される。
3. buttonを押し、LocalhostとBonjour selfがpopupへ出る。
4. Bonjour selfを選び、選択名がパネルへ反映される。
5. WebUIの選択表示も同じruntimeへ変わる。
6. モデルをWebUIで適用後、GarageBandの新規sessionとoffline Bounceを確認する。

## UNKNOWN

- GarageBand再起動後の新UI visual/Human Gate。
- 別Mac runtimeをAU popupから選んだ実network変換。
- Windowsは今回のAU固有UIをcompileしないが、大規模変更後のWindows回帰は資源待ち。
