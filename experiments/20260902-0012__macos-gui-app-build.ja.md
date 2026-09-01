# 実験票: macOS向けGUIクロスプラットフォーム化とAPP形式ビルド

実施時刻: 2026-09-02 00:12 JST
対象 Issue: #1, #5, #6 (先行), #10 (学習フォルダとの分離確認)
branch / HEAD: gui-macos-portability (worktree: /Users/saitoumitsuru/RVC-WebUI-gui)
実行環境: macOS 15.2, Xcode 16.2, cmake 3.31.3, Apple clang, x86_64
結果: success

## 目的

`RVCRealtimeVST` のGUI/設定レイヤーがWin32 APIへ直結しておりmacOSでビルドできない問題を解消し、
iPlug2 `FORMATS APP`（スタンドアロンアプリ）でmacOS上にGUIを表示できるかを確認する。
デルタもん歌唱model学習（PID 55307、`/Users/saitoumitsuru/RVC-WebUI` で継続中）とは
git worktreeで作業ディレクトリを分離し、学習中のfile/processに触れないことを前提とする。

## 入力・前提

- `src/RVCRealtime.cpp`: 設定保存/パス判定/64bit実行ファイル判定がすべて未ガードのWin32 API
  （`GetPrivateProfileStringW`, `GetFileAttributesW`, `GetBinaryTypeW`, `CreateDirectoryW`）
- `src/WorkerClient.cpp`: `#if !defined(_WIN32) #error ...` でWindows以外のコンパイルを拒否
- `CMakeLists.txt`: `FORMATS` に関係なくVST2/VST3 SDKの存在を無条件で要求、`LANGUAGES ... RC` はWindows専用
- `third_party/iPlug2` はsubmodule未取得の状態からスタート

## 実行コマンド

```text
git worktree add ../RVC-WebUI-gui gui-macos-portability
git submodule update --init RVCRealtimeVST/third_party/iPlug2
bash third_party/iPlug2/Dependencies/download-prebuilt-libs.sh
cmake -S . -B build-macos -G Xcode
cmake --build build-macos --target RVCRealtime-app --config Debug
open build-macos/out/Debug/RVCRealtime.app
```

## 観測事実

- パス結合/存在判定/64bit判定/INI設定保存を `std::filesystem` + 独自key=value形式へ置換し、
  Windows側のロジック（`#if defined(_WIN32)`）はそのまま温存した。
- `WorkerClient.cpp` を `WorkerClient_win.cpp` へrename、非Windows向けに
  `WorkerClient_stub_mac.cpp` を新設（実IPCは一切行わず、`ENGINE OFF`/passthroughのみ）。
  スタブである旨はコード内コメントとGUI上のステータス文字列
  （`macOS worker not implemented (GUI stub, see issue #3/#6)`）の両方に明記した。
- `CMakeLists.txt` の `RVC_FORMATS` をplatformごとに分岐（Windows: VST2/VST3、macOS: APP）し、
  VST SDK存在チェックも要求されたFORMATSに応じて条件化した。
- iPlug2側の `FORMATS APP` を使うために `resources/RVCRealtime-macOS-Info.plist`,
  `resources/RVCRealtime-macOS-MainMenu.xib`, `resources/main.rc_mac_dlg`,
  `resources/main.rc_mac_menu`, および `config.h` の `APP_*` マクロ一式を
  iPlug2公式サンプル（`Examples/IPlugVisualizer`）から移植・改名して追加した。
- `cmake --build` は `** BUILD SUCCEEDED **` で完走し、`RVCRealtime.app` が生成された。
- `open` で起動したプロセス（PID 67972）はDock/画面上に正常なウィンドウを表示し、
  利用者のスクリーンショットで以下を目視確認済み: ヘッダー、4本のパス選択行、
  9スロットのパラメータスライダーグリッド、F0手法・出力ゲイン・エンジントグル、
  スタブ状態メッセージがamber色で正しく表示。
- このセッションの `screencapture` はサンドボックス制約でexit 139（SIGSEGV）となり、
  agent自身によるスクリーンショット取得は失敗した。人間側の別スクリーンショットで代替確認した。
- 学習プロセス（PID 55307, `/Users/saitoumitsuru/RVC-WebUI` cwd）は本作業を通じて
  CPU使用率・存続とも影響を受けていないことを `ps` で確認した。

## 解釈 / 仮説

macOS向けGUI表示までの最短経路は、AUv2/DAW実機を待たずに `FORMATS APP` を使うことで妥当だった。
Win32依存の除去は「パス/設定/ファイル判定」の範囲では想定通り小さな差分で完了したが、
`WorkerClient.cpp` のコンパイル拒否は当初計画で見落としていた追加ブロッカーであり、
スタブ実装で切り離すことでGUI確認までの到達を早められた。

## Recovery / 次の一手

- 次はIssue #5本体（AUv2 `.component` ターゲット追加、`auval` 検証）へ進める。
- Issue #3/#6の実IPC実装が完了したら `WorkerClient_stub_mac.cpp` は削除し、
  `RVC_WORKER_CLIENT_SOURCE` の分岐も不要になる。
- Windows側ビルド（VST2/VST3）がこの変更で壊れていないかは、Windows実機での再検証が必要
  （このセッションはmacOS実機のみで検証したため未確認）。

## unknown

- macOS実機でのVST3ビルド（AU以前にVST3単体）は今回未検証。
- Windows側でのビルド再現は未検証（コードレビューでのAPI互換確認のみ）。
- Apple Silicon実機での挙動は未確認（今回はx86_64のみ）。
