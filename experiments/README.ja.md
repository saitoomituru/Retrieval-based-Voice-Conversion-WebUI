# 実験・開発ログ

この棚は `RVCRealtimeVST` の macOS / Audio Unit 移植で得た、成功・失敗・blocked・未試験を保存します。
OpenSourcePITETO の実験記録と同じく、失敗を消さず、観測と解釈を分けて次の追試へ渡します。

## 命名

```text
YYYYMMDD-HHMM__短い-slug.ja.md
```

## 最低限残すもの

- 実施時刻
- 対象 Issue
- branch / HEAD
- 実行環境
- 目的
- 入力・前提
- 実行コマンド
- 観測事実
- 解釈 / 仮説
- 結果
- Recovery / 次回
- unknown

## 結果語彙

- `success`: 今回の acceptance criteria を満たした
- `failure`: 実行できたが期待結果に届かなかった
- `blocked`: 前提問題により継続不能
- `not-tested`: まだ実行していない

## 自動生成

`scripts/record_experiment.py` は標準ライブラリだけで初期票を作ります。
`--run` を渡した場合は subprocess の exit code、stdout、stderr を receipt として保存します。

自動生成された文書は観測 receipt であり、README の claim や Issue 完了を自動更新しません。
