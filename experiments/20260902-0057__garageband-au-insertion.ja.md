# 実験票: GarageBand実機でのAUv2挿入確認

実施時刻: 2026-09-02 00:57 JST
対象 Issue: #7
branch / HEAD: gui-macos-portability, commit 8a3364f
実行環境: macOS (利用者実機), GarageBand, 実プロジェクト「村祭り」(DTM打ち込み+録音ボーカル)
結果: success (挿入・GUI表示のみ。automation/bypass/削除/mono-stereo/latencyは未実施)

## 目的

`auval`によるヘッドレス検証(20260902-0230実験票)に続き、実際のApple DAW host
(GarageBand)でAudio Unitを挿入・表示できるかを確認する。Issue #7の受入条件の
一部（挿入・生成できること）を検証する。

## 入力・前提

- `~/Library/Audio/Plug-Ins/Components/RVCRealtime.component` を手動配置済み（前実験）
- 利用者が用意した実プロジェクト「村祭り」（打ち込みトラック複数 + 録音済みボーカル
  トラック オーディオ2/オーディオ3）を使用。プロジェクトファイル自体はこのforkへ含めない

## 実行コマンド

利用者がGarageBand GUI上で直接操作（agent側のクリック操作は行っていない）。
- オーディオ3トラックを選択
- プラグインスロットで `Audio Units → RVC Realtime` を選択して挿入

## 観測事実

- 利用者提供のスクリーンショットで、オーディオ3トラックの「プラグイン」欄に
  Compressor / Channel EQ と並んで「RVC Realtime」が選択・表示されている。
- プラグインウィンドウ「オーディオ3」内にRVCRealtimeのGUIがGarageBand上で正常に
  描画されている: ヘッダー、4本のパス選択行、9スロットのパラメータスライダー、
  F0手法/出力/エンジントグル(ENGINE OFF)、スタブ状態メッセージ
  「macOS worker not implemented (GUI stub, see issue #3/#6)」も表示。
- GarageBand自体のクラッシュ・フリーズは報告されていない。

## 解釈 / 仮説

Issue #7の作業項目のうち「local AU buildを正しいmacOS component配置先へinstallする」
「GarageBandで読み込む」は本実験で確認できた。ただし受入条件全体
（挿入・生成・automation・bypass・削除ができること）のうち、automation書き込み、
bypass、プラグイン削除、mono vocal input/stereo host構成の試験、latency reporting/
timeline alignment確認は本実験の範囲外で、まだ実施していない。

## Recovery / 次の一手

- 同じプロジェクトでbypass・削除・パラメータautomationの書き込み再生を試験する。
- Logic Proでの同等確認はまだ未着手。
- worker/model errorでDAWが停止しないことの確認は、スタブが常に何もしないため
  「エラーが起きない」ケースの確認に留まり、実エラー時の挙動確認はIssue #6の
  実IPC実装後に必要。

## unknown

- automation/bypass/削除の実際の挙動
- mono入力時とstereo hostでの挙動差
- 報告latencyとtimeline alignment
- Logic Proでの同等確認
