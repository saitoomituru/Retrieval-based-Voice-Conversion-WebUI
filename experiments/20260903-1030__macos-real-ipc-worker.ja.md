# 実験票: macOS実IPC実装(issue #3)とworker統合(issue #6)

実施時刻: 2026-09-03 10:30 JST
対象 Issue: #3, #6
branch / HEAD: gui-macos-portability
実行環境: macOS 15.2, Xcode 16.2, cmake 3.31.3, x86_64, Python 3.12.7 (.venv)
結果: success (CLI/rvc-worker-smokeでの実証まで。GarageBand/Logic実機でのENGINE ON確認は未実施 — ヒューマンテスト工程)

## 目的

`WorkerClient_stub_mac.cpp`(GUI確認用スタブ)を実IPC実装(`WorkerClient_mac.cpp`)へ
置き換え、macOS実機上でWindows版と同じワイヤプロトコルによりPython worker
(`worker/rvc_worker.py`)を起動・通信・実推論できることを実証する。学習は
完全に終了しておりリスクなく着手できる状態だった。

## 設計判断

Windows版(`WorkerClient_win.cpp`)のプロトコル自体(ヘッダレイアウト、リング
バッファのoffset、`kMagic`等)はOS非依存だったため、変更したのはOSプリミティブ
3種のみ。

| Windows | macOS実装 |
|---|---|
| `CreateFileMappingW`+`MapViewOfFile` | `shm_open`+`ftruncate`+`mmap` |
| `CreateEventW`+`SetEvent`+`WaitForSingleObject`(named event) | **共有メモリ上のsequence番号をpollingで監視**(named semaphore/eventは使わない) |
| `CreateProcessW` | `posix_spawn` |
| `GetTempPathW` | `getenv("TMPDIR")`(未設定なら`/tmp`) |

named event/semaphoreを使わずpollingにした理由: Python標準ライブラリには
POSIX named semaphoreの公開APIがなく(`posix_ipc`という追加pip依存が必要になる)、
block sizeが最短でも20ms以上ある前提でpolling間隔1msは無視できるlatency増加に
留まるため。`worker/rvc_worker.py`側は`multiprocessing.shared_memory`
(stdlib、POSIX `shm_open`をラップ)と新設`PosixSequenceWaiter`クラスで対応。

worker scriptパスは`iplug::BundleResourcePath`(bundle ID経由)で解決。
`CMakeLists.txt`のRESOURCESへ`worker/rvc_worker.py`を追加し、
`Contents/Resources/rvc_worker.py`へ配置されるようにした。

## 実行コマンド

```text
cmake --build build-macos --target rvc-worker-smoke --config Debug

export RVC_WORKER_SCRIPT=.../worker/rvc_worker.py  # bundleでないrvc-worker-smoke用の開発時override
build-macos/Debug/rvc-worker-smoke \
  /Users/saitoumitsuru/RVC-WebUI \
  /Users/saitoumitsuru/RVC-WebUI/.venv/bin/python \
  /Users/saitoumitsuru/RVC-WebUI/assets/weights/deltamon_singing_40k_v2_20260901_e20.pth \
  "" 130 80 1

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 観測事実

- 初回実行は`shm_open`名の二重スラッシュ不整合(`multiprocessing.shared_memory`が
  内部で自前に`/`を前置する仕様と、C++側が既に`/`付きの名前を渡していたことの
  衝突)で`FileNotFoundError`となった。C++側の名前生成を`/`なしに修正して解消
  (詳細はcommit参照)。
- 修正後、デルタもんモデル(epoch20)で実行: `READY frames=6240 latency=12480
  infer_ms=549.306 output_rms=0.0229491 drops=0 blocks=1
  status="Ready (actual CF 40 ms)"` — 無音でない実音声が返り、drop 0件。
- 複数ブロック(3)、ずんだもんモデルでも同様に成功(output_rms 0.0524297 / 0.0363582、
  drop 0件)。**モデル切替も実データで動作確認できた**。
- `RVCRealtime-app`/`RVCRealtime-au`とも実`WorkerClient_mac.cpp`込みでbuild成功。
  `Contents/Resources/rvc_worker.py`がbundle内に配置されていることを確認。
- `auval -v aufx Rvcr Rvcp`はAU VALIDATION SUCCEEDEDのまま(ENGINEがデフォルトOFFの
  ためworker起動自体は発生しない経路)。

## 解釈 / 仮説

issue #3の受入条件(「実験用audio/control messageの往復」)と、issue #6の受入条件
(「plug-inがmacOS workerを起動し、有効な外部RVC model/indexを読み込み、短い
vocal bufferを処理してplug-in processへaudioを返せること」)は、CLIツール
(`rvc-worker-smoke`)経由で実モデル・実推論により満たしたと判断する。

ただし`workerScriptPath()`の`BundleResourcePath`経由の解決(実際のAU/APP bundle内
からの起動)は、`rvc-worker-smoke`が`RVC_WORKER_SCRIPT`環境変数で迂回しているため
**未検証**。GUIからGarageBand実機でRVC ROOT/PYTHON/MODELを設定しENGINEをONにする
操作は、この作業のヒューマンテスト工程として利用者側に委ねる。

## Recovery / 次の一手

- GarageBand/Logic実機でRVC ROOT(macOSの.venv構成)・MODELを設定し、ENGINEを
  ONにして実際に声質変換が動くか確認する(ヒューマンテスト)。
- 実機でエラーが出た場合、bundle経由のBundleResourcePath解決が主な疑わしい箇所。
- issue #7の残タスク(automation/bypass/latency計測)もこの実workerで再検証できる。
- issue #8(端から端まで検証)は、このヒューマンテストの結果を踏まえて判断する。

## unknown

- 実際のbundle(.component/.app)からのworkerScriptPath()解決(BundleResourcePath)
- GarageBand/Logic実機でのリアルタイム変換品質・latency体感
- Apple Silicon実機
