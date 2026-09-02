# 実験票: BAKEトグルのfreeze回帰と、RVC ROOT/PYTHON/MODEL自動検出の問題

実施時刻: 2026-09-02 19:22 JST (利用者側観測), 記録は後追い
対象 Issue: #14 (回帰), 新規issueへ切り出し(PYTHON自動検出のWin依存, MODEL/INDEX自動検出欠如)
branch / HEAD: gui-macos-portability, commit 3080c5b時点で発生 → 本記録時点でfixを追加
実行環境: macOS実機, GarageBand, 実プロジェクト「村祭り」
結果: blocked (freezeの修正はコード変更のみ、実機再検証は未実施のためunverified)

## 目的

issue #14で追加したREALTIME/BAKEインジケータの実機テスト結果と、RVC ROOT選択時の
自動検出まわりの問題を記録する。

## 観測事実（利用者提供スクリーンショット, 19:22:46 / 19:23:47 / 19:27:44）

1. プラグインロード直後、BAKEトグルボタンを押すと**そのボタンが機能しなくなる
   （フリーズ/固まる）**。ボタン表示自体も、テキストのない無地の白い矩形になっている
   （他の`...`ボタン群は正常にteal色+ラベルで描画されている）。
2. RVC ROOTに`/Users/saitoumitsuru/RVC-WebUI`を選択したところ、
   PYTHONが自動検出されず、`STATUS`が`CONFIG ERROR`、詳細に
   `runtime\python.exe not found; select Python manually.`と表示された。
3. WebUI(`webui.py`)側のスクリーンショットでは、`Inferencing voice`ドロップダウンに
   デルタもんの複数checkpointが自動列挙されている。

## 解釈 / 仮説

- (1)について: `SetLabelStr()`をOnIdle()から毎フレーム呼ぶ実装が原因の可能性が高いと
  推測したが、`OnStyleChanged()`はIVButtonControlでは実質NO-OPであり、コード上で
  確定的な原因特定はできなかった。**このセッションはscreencaptureが使えず自分では
  再現・目視確認できないため、確定した原因ではなく推測に留まる。**
  対処として、このファイル内で実機検証済みの安全なパターン（`kCtrlStatus`/
  `kCtrlPerformance`と同じ、`ITextControl`+`SetStr()`を毎フレーム呼ぶ形）へ置き換え、
  `IVButtonControl`側のラベルは初期化後に二度と書き換えない静的ラベル("MODE")に変更した。
  ビルド(APP/AU)は成功したが、**実機での再現確認はできておらず、この修正が
  実際に直したかは未検証**。
- (2)について: `SetRvcRoot()`は`<root>/runtime/python.exe`というWindows版RVC同梱Python
  の前提をハードコードしており、macOSの実際の構成(`docs/macos-runtime-bootstrap.ja.md`の
  `.venv/bin/python`)への分岐が存在しない。これは確定した移植漏れ。
- (3)について: `webui.py`は`os.listdir(weight_root)`(`assets/weights`)でモデルを
  自動列挙しドロップダウン化しているが、`RVCRealtimeVST`側のMODEL/INDEX選択には
  この機能がWindows版含めて元から存在しない。移植漏れではなくフォーク自体の
  既存の機能ギャップ。

## Recovery / 次の一手

- freeze修正の実機再検証（利用者の任意のタイミングで）
- PYTHON自動検出のmacOS対応（`.venv/bin/python`探索の追加）を別issueで対応
- MODEL/INDEX自動検出機能の追加を別issueで対応。VST2/VST3(Windows)側はWindows実機が
  手元にないため保守しない方針とし、Windows側の検証はOSSコミュニティの貢献
  （本家からのcommit、有志のcontribution、Windows実機の提供等）を歓迎する形で
  issueに明記する。AU(macOS)側のみこちらで対応する。

## unknown

- freeze修正が実際に直っているか（未検証）
- freezeの厳密な原因（推測のみ、確定していない）
