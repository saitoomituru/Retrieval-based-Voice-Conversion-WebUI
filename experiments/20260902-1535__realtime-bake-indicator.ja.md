# 実験票: REALTIME/BAKEモードインジケータ(issue #14)の先行GUI実装

実施時刻: 2026-09-02 15:35 JST
対象 Issue: #14
branch / HEAD: gui-macos-portability (worktree: /Users/saitoumitsuru/RVC-WebUI-gui)
実行環境: macOS, Xcode 16.2, cmake 3.31.3, x86_64
結果: success (GUI要素の追加・build・auvalのみ。実機での自動切替観測はまだ)

## 目的

issue #14で決めたA案（`GetRenderingOffline()`自動反応 + 手動オーバーライドトグル +
MODEインジケータ）のうち、GUI要素部分を先行実装する。実処理側(worker品質分岐)は
issue #3/#6完了後のスコープとして今回は含めない。

## 入力・前提

- Zundamon学習(PID 5865, `/Users/saitoumitsuru/RVC-WebUI`)が稼働中。着手前に
  システム負荷を確認: 12コア中、学習が約584%(5.8コア)使用、load average 9.8前後、
  空き物理メモリ約1.8GB相当。同条件下で本日すでにAPP/AUビルドとauvalを計2+1回
  成功させている実績があり、追加のGUI変更によるビルドも同等リスクと判断して着手した。

## 実行コマンド

```text
# RVCRealtime.h: kCtrlRenderMode追加、mForceBakeMode(atomic<bool>)追加
# RVCRealtime.cpp: ヘッダーへIVButtonControl(REALTIME/BAKE)を追加、
#                   OnIdle()でGetRenderingOffline()||mForceBakeModeに応じてSetLabelStr

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 観測事実

- APP/AU双方とも `** BUILD SUCCEEDED **`。
- `auval -v aufx Rvcr Rvcp` は再度 `AU VALIDATION SUCCEEDED` で完走。
- 学習プロセス(PID 5865)はビルド前後で継続稼働(576.7%, 経過時間継続増加)を確認、
  中断・速度異常なし。
- ボタンクリックによる手動トグル、およびLogic/GarageBandでの実際のバウンス時に
  自動切り替わるかは、この実験では未検証（GUI描画とビルド健全性のみ確認）。

## 解釈 / 仮説

システム負荷が高い状態(load average ~9.8, 空きメモリ~1.8GB)でも、今回程度の
小規模GUI変更のビルドは学習プロセスへ影響を与えないことを追加で確認できた。

## Recovery / 次の一手

- 手元でボタンクリックによる手動トグルの目視確認。
- Logic Pro/GarageBandでFreeze/Bounceを実行し、自動でBAKE表示へ切り替わるかの実機確認。
- 確認できたらissue #14へreceiptとして記録、受入条件の該当項目をクローズ。

## unknown

- 手動トグルのクリック挙動の実機確認
- ホストのオフラインレンダー通知による自動切替の実機確認
