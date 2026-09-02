# 実験票: MODEL/INDEX自動検出機能の実装(issue #16)

実施時刻: 2026-09-02 21:45 JST
対象 Issue: #16
branch / HEAD: gui-macos-portability
実行環境: macOS, Xcode 16.2, cmake 3.31.3, x86_64
結果: success (build/auvalのみ。実機でのメニュー選択確認はまだ)

## 目的

webui.pyの`os.listdir(weight_root)`によるモデル自動列挙を、RVCRealtimeVSTの
MODEL/INDEX選択GUIへ移植する。VST2/VST3(Windows)側は対象外、AU(macOS)側のみ。

## 実行コマンド

```text
# RVCRealtime.h: kCtrlModelMenu/kCtrlIndexMenuタグ、RescanFileMenus()宣言を追加
# RVCRealtime.cpp:
#   - RVCFileMenuControl(IDirBrowseControlBase派生)を新規追加
#     - 非再帰スキャン(webui.pyのos.listdirと同じ、サブフォルダは辿らない)
#     - クリックでCreatePopupMenu、選択でSetModelPath/SetIndexPathへコールバック
#     - ディレクトリが空/存在しない場合はNItems()==0でクリックしても何もしない
#   - MODEL/INDEX行のパネル幅を692->654へ縮小し、"▾"スキャンボタン(658-694)を追加
#     (手動browseの"..."ボタン700-750はそのまま維持)
#   - RescanFileMenus(): <rvcRoot>/assets/weights, <rvcRoot>/assets/indices を
#     UpdateFileLabels()から都度re-point(RVC ROOT/MODEL/INDEXいずれの変更後も)

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 観測事実

- APP/AUともbuild成功、auval成功。
- `/Users/saitoumitsuru/RVC-WebUI/assets/weights`に実際のデルタもんcheckpoint
  (`.pth`)が複数存在することを確認した(webui.py側のドロップダウンで既に
  表示されていたものと同じ)。`assets/indices`は空だった。
- 学習プロセス(PID 5865)は作業中も継続稼働(11:39経過時点で532.6%)、影響なし。

## 解釈 / 仮説

buildと`auval`は成功したが、**実際にRVC ROOTへ`/Users/saitoumitsuru/RVC-WebUI`
相当を設定し、"▾"ボタンでcheckpoint一覧が表示・選択できるかは、このセッションの
screencapture制約により自分では確認できていない**。次の実機テストで確認が必要。

## Recovery / 次の一手

- 実機で"▾"ボタンをクリックし、`assets/weights`内の`.pth`一覧が出るか、
  選択でMODELフィールドが更新されるかを確認する
- `assets/indices`が空のケース(今回のデルタもん)でクリックしても何も起きない
  (NItems()==0でCreatePopupMenuを呼ばない)ことも合わせて確認する

## unknown

- 実機でのメニュー表示・選択動作
- 空ディレクトリでのクリック時の実際の見た目(視覚的フィードバックがない点が
  UX上気になる場合、別途改善の余地あり)
