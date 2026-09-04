# upstream PR前の穴抜け監査

## 目的

GarageBand Human Gate合格後のforkが、RVC本体とiPlug2のupstream PRを作れる段階か判定する。PR本文は日本語と繁體中文を併記し、英語本文を正本にしない。

## 対象

- RVC fork `main`: `672aa64`
- RVC upstream提出用branch: `upstream/macos-au-webui-runtime` / `eaa6b46`
- RVC upstream: `origin/main`（2026-09-05 fetch）
- iPlug2 fork: `fix/skip-midi-init-without-midi` / `cbeba6cff704bdedba74166af6f6ea979bada5df`
- iPlug2 upstream: `upstream/master`（2026-09-05 fetch）
- 親Issue: #1、PR準備Issue: #9

## 観測事実

### RVC

- upstream提出用branchは`origin/main`起点で、71 files、約5,774 insertions / 218 deletions。
- `git diff --check origin/main...upstream/macos-au-webui-runtime` は成功。
- tracked model、index、`.venv`、runtime log、TEMPは検出されず、`.gitignore`適用を確認した。
- 1 MiB以上の追加物はfork広報用画像 `assets/fusamofu-img/AUv2inside.png`（2,969,293 bytes）のみ。
- 実験票にはlocal absolute pathが含まれるが、汎用source内に新規の開発者固有pathは検出されなかった。上流既存の`infer/rmvpe.py`にはWindows開発path例が残るが今回差分ではない。
- push済みbranchのclean recursive cloneで全Python回帰43件、macOS APP/AU Release build、codesign、auvalが合格した。
- GarageBand offline Bounce、model切替、設定slot復元は既存Human Gateで合格済み。

### iPlug2

- 最新`upstream/master`をmerge済みで、実差分は一ファイルだけである。
- 差分は `IPlug/APP/IPlugAPP_host.cpp` の17追加 / 1削除。
- `merge-tree --write-tree HEAD upstream/master` はtreeを生成し、text conflictを検出しなかった。
- fork/upstream双方に同branch由来の既存PRはない。
- patchはMIDI input/outputを双方無効にしたAPP hostだけでCoreMIDI初期化とnull MIDI device選択をskipする。

## 提出境界

### 解消したblocking

1. `origin/main`起点の目的branchへ汎用source、tests、最小docsだけを抽出した。
2. Sphere-DOS、fork運用AGENTS、実験履歴、外部agent原稿、広報画像、character固有receiptを上流branchから除外した。
3. 旧macOS embedded Python workerと未実装stubを除外し、WebUI所有RSVC thin headへ一本化した。
4. push済みbranchを空directoryへrecursive cloneし、全submoduleの取得、43 tests、clean APP/AU build、codesign、auvalを確認した。
5. recursive clone中に検出したVST SDK gitlink誤記、gitfile誤判定、未署名APP、bundle設定警告を修正して再検証した。

### 解消済み・方針訂正

- iPlug2 branchは最新`upstream/master`へmergeし、forkの`cbeba6cff704bdedba74166af6f6ea979bada5df`へpushした。実差分は引き続き`IPlugAPP_host.cpp`一ファイル。
- Bonjour/mDNSのsegment到達性、CIDR、越境はOS、network、router、reflectorの責務とする。RVCは独自SaaS認証やsegment policyを抱えず、信頼済み制作LAN前提と非保証範囲を説明する。現行のBonjour/LAN機能をloopback限定へ後退させない。
- clean checkoutではmodel再学習・長時間training・実モデル再生成を繰り返さない。modelなしdry-run、unit/integration test、CMake configure、APP/AU build、codesign、auvalまでを再現性対象とし、学習・実推論・聴感は既存receiptを参照する。

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

1. 利用者が日本語 + 繁體中文のRVC PR草案をreviewする。
2. 利用者が日本語 + 繁體中文のiPlug2 PR草案をreviewする。
3. 修正指示を反映後、利用者の送信判断で各upstream PRを作る。

## 結果

`ready-for-human-pr-review`。実装、提出物境界、clean再現性検証、二言語草案まで完了した。Windows、Apple Silicon、Logic Pro、別Mac、realtime性能はPRを止める不具合ではなく、明示済みの資源境界・既知制限・後続課題である。実PRは利用者review前のため未送信。
