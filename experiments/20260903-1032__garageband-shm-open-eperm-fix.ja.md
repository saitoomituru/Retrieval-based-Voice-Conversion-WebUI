# 実験票: GarageBand実機でshm_open EPERM失敗を修正(issue #3/#6)

実施時刻: 2026-09-03 10:32 JST (利用者観測), 記録は後追い
対象 Issue: #3, #6
branch / HEAD: gui-macos-portability
実行環境: macOS実機, GarageBand
結果: success (修正・CLI再検証まで。GarageBand実機での再検証は利用者側で今後実施)

## 目的

commit 9e68cdbの実IPC実装をGarageBand上でENGINE ONにしたところ、
`IPC initialization failed (shm_open errno 1)`のERRORが出て声質変換が
発生しなかった。原因を特定し修正する。

## 観測事実

- 利用者提供スクリーンショットで、RVC ROOT/PYTHON/MODELはすべて正しく設定
  されており(PYTHON自動検出も正常)、ENGINEをONにした直後にSTATUSが`ERROR`、
  詳細に`IPC initialization failed (shm_open errno 1)`と表示された。
- errno 1 = EPERM。
- `$TMPDIR/RVCRealtime/logs/`を確認したところ、このセッションの前に実施した
  CLI(`rvc-worker-smoke`)テスト分のinstanceファイルのみが存在し、GarageBand
  セッション由来のconfigファイルは一件も生成されていなかった。
  `launchWorker()`の実装順序上、`shm_open`失敗は`writeWorkerConfig()`より
  前で起きるため、config書き出しまで到達していないことと整合する。

## 解釈 / 仮説

GarageBandはAUv2プラグインをプロセス内(in-process)でホストしており、GarageBand
自体がApp Sandbox配下で動作しているため、`shm_open(..., O_CREAT, ...)`による
POSIX名前付き共有メモリの新規作成がサンドボックスにより拒否された
(`rvc-worker-smoke`は非sandboxな素のCLIプロセスのため同じ呼び出しが成功していた)。

## 対応

`shm_open`(名前付き共有メモリオブジェクト)をやめ、`$TMPDIR`配下の**通常ファイル**を
`open`+`ftruncate`+`mmap(MAP_SHARED)`する方式へ変更した。$TMPDIR配下への通常
ファイルI/Oはsandbox下でも許可される標準的な書き込み領域であるため。

- `WorkerClient_mac.cpp`: `Ipc::shmFd`/`shmName` → `Ipc::mapFd`/`mapPath`
  (`<configPath>.shm`という通常ファイル)。cleanup も`shm_unlink()`→`unlink()`。
- `worker/rvc_worker.py`: `multiprocessing.shared_memory.SharedMemory`(内部で
  shm_open相当)をやめ、`os.open(args.map, os.O_RDWR)` + `mmap.mmap(fd, MAP_BYTES)`
  という素朴なfile-backed mmapへ変更。副次効果として、以前あった
  「Pythonの`SharedMemory`が名前へ`/`を自動前置する」仕様との二重スラッシュ
  問題（今回の実装より前に発生していた別の不具合、既にcommit 9e68cdbで対応
  済みだったもの）も構造的に解消され、Windows側と同様に`shared`が常に
  `mmap.mmap`オブジェクトになったためcleanup処理も統一できた。

## 実行コマンド

```text
cmake --build build-macos --target rvc-worker-smoke --config Debug
RVC_WORKER_SCRIPT=.../worker/rvc_worker.py build-macos/Debug/rvc-worker-smoke \
  /Users/saitoumitsuru/RVC-WebUI /Users/saitoumitsuru/RVC-WebUI/.venv/bin/python \
  /Users/saitoumitsuru/RVC-WebUI/assets/weights/deltamon_singing_40k_v2_20260901_e20.pth "" 130 80 1

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 結果

- CLI再検証: `READY frames=6240 latency=12480 infer_ms=584.193
  output_rms=0.0252917 drops=0 blocks=1` — 修正後も正常動作。
- APP/AUともbuild成功、auval成功。

## Recovery / 次の一手

GarageBand実機で再度ENGINEをONにし、`shm_open`エラーが解消され実際に声質変換
されるか確認する(ヒューマンテスト継続)。

## unknown

- GarageBand実機での修正後の動作(今回はCLIでの再現のみ確認、実機は未検証)
- 今回のsandbox仮説が唯一の原因か、他のsandbox制約が別途あるかは未確認
