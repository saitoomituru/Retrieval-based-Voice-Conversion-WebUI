# Bonjour経路切替後のGarageBand offline bounce Human Gate

- 対象 Issue: #29 #31 #33 #35
- 対象 branch / HEAD: `main` / `a9c59ec`
- 実行環境: Intel Mac Pro、macOS 15.7.7、GarageBand 10.4.14、AUv2 `RVCRealtime`
- 実行者: saitoomituru（聴感判定）、Codex（実装・記録）
- 日時: 2026-09-04 20:12–20:13 JST
- 結果: `bonjour-self-route-human-pass / realtime-known-failure / offline-human-pass`

## 目的

WebUI/controllerがBonjourで発見した自己runtimeへ明示的に経路を切り替えた後も、AU thin headから実RVC engineまで音声が到達し、GarageBand標準offline bounceで変換済み音声を書き出せることを確認する。

## 入力と操作

1. GarageBand内のRVCRealtime AUをENGINE ONにした。
2. AUの`RUNTIME / SCAN / SELECT`から`Bonjour: RVC WebUI saitoomiturunoMac-Pro 26c0b2`を選択した。
3. realtime再生を行った。
4. GarageBand標準のバウンスを実行した。
5. 書き出し音声を利用者が試聴し、原音との差、変換効果、プチプチの有無を判定した。

## 観測事実

- AUはBonjour自己serviceを選択した状態で`READY`へ復帰し、RSVC session 6を表示した。
- realtime再生では約`1297 ms / 530 drop / 0 off`を表示し、利用者はプチプチを聴取した。
- offline bounce中は`OFFLINE`となり、約`1317 ms / 570 drop / 1 off`を表示した。
- バウンスは完了した。
- 利用者の書き出し後リスニングでは、RVCエフェクトが正しく効き、プチプチはなく滑らかに再生された。
- 画面のdrop値はrealtimeからの累積表示であり、offline block自体が570回dropしたことの証拠ではない。offline専用counter分離は未実装である。

## 解釈

- discovery、明示選択、確立済みsession切断、新session再確立、gateway経由の実RVC推論、host標準offline renderが一つの経路として成立した。
- Bonjour自己serviceは同一Mac上の同一engineへrouteするため、Localhost選択との声質差がないことは正常である。
- realtimeのdeadline超過はBonjour固有障害ではなく、同一MacのIntel CPU推論時間がblock deadlineを超える既知の#35である。
- offline bounceの聴感合格は、GarageBandが処理完了を待てる経路では推論品質を保持できることを示す。

## 未確認 / 凍結

- 別Mac間の実LAN transport、Wi-Fi断、実LAN RTT/drop、Windows Bonjour相互運用は検証資源がない。
- これらは通常の未解決実装ではなく、複数実機または外部contributorという外部事象で再開するペインステータス凍結として#31へ残す。
- Logic Pro、Apple Silicon、配布appのLocal Network promptも同様に本実測の射程外である。

## Recovery / 次の一手

- realtime品質は#35で性能改善を継続する。
- offlineとrealtimeのdrop表示を分離する場合は診断UIの別Issueとする。
- 次のHuman GateはAU hot parameterがRSVC `CONFIG_UPDATE`で実RVC engineへ反映されることを、pitch A/B bounce等で確認する。
