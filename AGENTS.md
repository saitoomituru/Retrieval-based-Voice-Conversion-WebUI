# AGENTS.md — macOS / Audio Unit 移植作業規約

この文書は、この fork で人間と coding agent が `RVCRealtimeVST` の macOS / Audio Unit 対応を進めるための局所正本です。
上流 RVC の一般仕様を置き換えません。

## 目的

- 既存 `RVCRealtimeVST` の Windows 実装を壊さず macOS へ移植する
- iPlug2 の Audio Unit 対応を成立させ、Logic Pro / GarageBand で検証する
- 汎用化できる差分は upstream PR へ戻せる形に保つ
- キャラクター固有モデル、学習データ、作品都合を上流汎用コードへ混ぜない

## 必読順序

1. この `AGENTS.md`
2. `README.md` 冒頭の fork 方針
3. Issue #1 と作業対象の子 Issue
4. `RVCRealtimeVST/README.en.md` または最寄りの README
5. ZeroRoomLab-manifest の公開 `AGENTS.md` と、必要な運用文書
6. SphereOS-Atlantis の PLI / Sphere-DOS 文書を参照する場合は、その実装状態と証拠境界

外部 repository の規約を全文コピーして正本化しません。参照元、revision、読んだ範囲を作業ログへ残します。

## 自動開発の基本規約

- 変更前に対象 Issue、現在の diff、既存ツール、近傍 README を確認する
- 他 agent / user の未コミット差分を推測で消さない。`reset --hard`、`clean -fd`、無断 rebase をしない
- Windows を既定正常系としても、macOS を例外系としても扱わない。platform 差分は adapter / 条件分岐として明示する
- NVIDIA / CUDA を macOS 側の暗黙前提にしない。CPU、ONNX、利用可能な backend を実測で分ける
- Audio thread へ blocking I/O、Python 待ち、network、重い allocator を持ち込まない
- WebUI 起動だけを VC 成功と判定しない。実音声の end-to-end 変換を別検証する
- upstream へ戻す変更と、この fork 固有の実験を混ぜない
- 未実装、未試験、推測を実装済みと書かない
- 失敗は消さず、再現条件と Recovery を `experiments/` へ残す

## 日本語運用

- この fork の新規開発文書、Issue、実験ログ、通常の commit message は常用令和日本語を既定とする
- API 名、class 名、file path、CMake target、規格名、GitHub が機械的に要求する文字列は原文を保持する
- upstream へ提出する PR 本文や upstream 側の既存言語規約が英語を要求する場合は、その境界だけ英語を使う
- 英語でなければ機械的問題を起こす箇所以外で、理由なく英語へ逃げない

## 実験・開発ログ

`experiments/` は成功例だけでなく、失敗、blocked、未試験、比較結果を保存する棚です。

記録では最低限、次を分離します。

- 目的
- 対象 Issue / commit / branch
- 実行環境
- 入力
- 実行コマンド
- 観測事実
- 解釈 / 仮説
- 結果: success / failure / blocked / not-tested
- Recovery / 次の一手
- unknown

会話ログだけを引継ぎ正本にしません。

## Semantic Stop / 致命的問題

次のいずれかを検出した場合は、変更を拡大せず停止し、日本語で詳細 Issue を作成します。

- 上流 license / third-party license と予定変更の両立が確認できない
- Windows 互換を壊すしかないが、Issue の目的から正当化できない
- DAW audio thread を恒常的に block する設計しか成立しない
- macOS sandbox / AU 制約により現行 worker architecture が成立しないことが確認された
- 利用者の秘密情報、モデル、学習データを意図せず repository へ含める危険がある
- 実測と README / Issue の claim が重大に矛盾する
- 対象 branch / upstream 差分 / 未コミット変更の所有関係が解けない

Issue には、観測事実、再現手順、影響範囲、試した Recovery、unknown、再開条件を含めます。

## Sphere-DOS / PLI 境界

この repository の `SPHERE-DOS.ja.md` は Prompt Line Interface 用の作業机契約です。
standalone OS runtime、daemon、scheduler、model runtime を実装済みとは主張しません。

PLI は自然言語で目的・制約・証拠・停止条件を渡す正規操作面として扱い、CLI の偽物とは呼びません。
機械的合否は test / validator / build / DAW 実機確認へ委譲し、PLI の説明だけで成功判定しません。

## 完了時の引継ぎ票

```text
対象 Issue:
対象 branch / HEAD:
変更した責務:
既存実装を再利用した箇所:
platform 固有差分:
実行したコマンド:
機械検証:
実機検証:
生成した experiment log:
upstream へ戻せる差分:
unknown / blocked:
次に触る Issue:
```
