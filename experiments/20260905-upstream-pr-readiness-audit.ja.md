# upstream PR前の穴抜け監査

## 目的

GarageBand Human Gate合格後のforkが、RVC本体とiPlug2のupstream PRを作れる段階か判定する。PR本文は日本語と繁體中文を併記し、英語本文を正本にしない。

## 対象

- RVC fork `main`: `39e6313`
- RVC upstream: `origin/main`（2026-09-05 fetch）
- iPlug2 fork: `fix/skip-midi-init-without-midi` / `d37c56917`
- iPlug2 upstream: `upstream/master`（2026-09-05 fetch）
- 親Issue: #1、PR準備Issue: #9

## 観測事実

### RVC

- upstreamに対して0 commit behind / 96 commit ahead。
- 総差分は122 files、約11,143 insertions / 218 deletions。
- `git diff --check origin/main...main` は成功。
- tracked model、index、`.venv`、runtime log、TEMPは検出されず、`.gitignore`適用を確認した。
- 1 MiB以上の追加物はfork広報用画像 `assets/fusamofu-img/AUv2inside.png`（2,969,293 bytes）のみ。
- 実験票にはlocal absolute pathが含まれるが、汎用source内に新規の開発者固有pathは検出されなかった。上流既存の`infer/rmvpe.py`にはWindows開発path例が残るが今回差分ではない。
- 全Python回帰43件、macOS AU Release build、codesign、auval、GarageBand offline Bounce、model切替、設定slot復元は合格済み。

### iPlug2

- upstreamに対して2 commit behind / 2 commit ahead。
- 差分は `IPlug/APP/IPlugAPP_host.cpp` の17追加 / 1削除。
- `merge-tree --write-tree HEAD upstream/master` はtreeを生成し、text conflictを検出しなかった。
- fork/upstream双方に同branch由来の既存PRはない。
- patchはMIDI input/outputを双方無効にしたAPP hostだけでCoreMIDI初期化とnull MIDI device選択をskipする。

## PRを今すぐ送らない理由

### blocking

1. RVC `main`は開発正本であり、Sphere-DOS、fork運用AGENTS、全実験履歴、Grok設計原稿、広報画像、character固有receiptを含む。そのまま上流へ送るとreview不能で、Issue #9の受入条件にも反する。
2. upstream用の目的branchを`origin/main`から作り、汎用source、tests、最小docsだけを抽出する必要がある。
3. iPlug2 branchは最新2 commitへ追従し、そのtreeでRVC APP/AUを再buildする必要がある。
4. 現forkはDarwin runtimeを既定`0.0.0.0` bindし、認証なしでBonjour広告する。研究環境の意図には合うが、局所規約の「localhost既定、LAN公開は明示」と矛盾する。upstream抽出ではloopback既定、明示opt-in時だけLAN bind/Bonjour広告にする。
5. clean recursive checkoutからのconfigure/build/test receiptがまだない。既存build cacheだけではsubmodule再現性を証明できない。

### non-blocking / 明記して提出可能

- Windows実機回帰、Logic Pro、Apple Silicon、別Mac Bonjourは資源未提供。UNKNOWNとしてtest matrixに残す。
- Intel Mac realtime CPUはdeadline未達でdropoutする。GarageBand標準offline Bounceはdrop 0でHuman Gate合格しており、#35の性能課題として分離する。
- 複数clientの排他、公平性、資源予約、SaaS tenant/session orchestrationは初期local制作環境の保証外。
- LaunchServices自動起動は#28で未実装。WebUI先行手動起動を前提として明記できる。

## 上流RVC branchへ含める候補

- `RVCRealtime/` の共通VST/AU sourceとbuild定義。ただし廃止予定のlegacy embedded mac workerは除外または明確にfollow-up化する。
- `rvc_realtime_engine.py`、RSVC/runtime/supervisor/gateway/control/Bonjour modules。
- `webui.py`、必要なengine/training portability差分。
- Python unit/integration tests、macOS requirements、最小build/runtime文書。
- model、local path、character固有名、全実験履歴、Sphere-DOS、fork AGENTS、広報画像は含めない。

## PR言語契約

- RVC upstream PR: 日本語 + 繁體中文。
- iPlug2 upstream PR: 日本語 + 繁體中文。
- source identifier、API、class、規格名、commandは原文維持。
- 両言語で同じ事実強度、既知制限、試験matrix、review依頼を記載する。

## 次の実行順

1. iPlug2 purpose branchを最新`upstream/master`へ非破壊統合し、APP/AU buildを再検証する。
2. `origin/main`起点のRVC upstream purpose branchを別worktreeに作り、汎用差分だけを抽出する。
3. loopback既定 / LAN明示opt-inを実装し、localhostと明示Bonjour self-routeを再検証する。
4. clean recursive checkoutでPython tests、CMake configure、APP/AU build、codesign、auvalを再実行する。
5. 日本語 + 繁體中文の2件のPR本文draftを、確定branch/commit/test receiptへ合わせて作る。
6. 利用者review後にのみupstream PRを送信する。

## 結果

`blocked-before-submission`。機能とHuman GateはPR級だが、upstream向け差分抽出と再現性検証が未完了であり、現mainを直接PRにする段階ではない。blockerは実装不成立ではなく提出物の境界整理である。
