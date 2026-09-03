# 実験票: GarageBand実機でのOpenMPスレッド生成クラッシュ修正(issue #6)

実施時刻: 2026-09-03 10:48 JST (利用者観測), 記録は後追い
対象 Issue: #6
branch / HEAD: gui-macos-portability
実行環境: macOS実機, GarageBand
結果: success (修正・CLI再検証まで。GarageBand実機での再検証は利用者側で今後実施)

## 目的

commit 5376603(KMP_DUPLICATE_LIB_OK=TRUE追加)後もGarageBand実機で同一の
クラッシュ(`libiomp5.dylib`の`__kmp_abort_process`、SIGABRT)が再発した。
`.process.log`から実際の失敗タイミングを特定し、より的確な修正を行う。

## 観測事実

- クラッシュしたworker instanceの`.process.log`を確認したところ、
  `所要時間: features=...s, index=...s, pitch=...s, model=...s`という
  推論タイミングのdebug出力が**2回のみ**記録され、その後何も出力されずに
  プロセスが終了していた。
- `infer/rtrvc.py`の`RVC.infer()`は`report_status = self.infer_count < 3 or
  self.infer_count % 100 == 0`で、最初の3回(count 0,1,2)は必ずこのログを
  出す設計。2回しか出ていないということは、**3回目のinfer()実行中に
  クラッシュした**ことを意味する(1回目=engine.prewarm()、2回目=実際の
  最初のaudio block)。
- 冷たいstart時のモデルロード・初回推論(engine.prewarm())は正常完了して
  おり、`KMP_DUPLICATE_LIB_OK`で想定していた「import時の重複ロード警告」
  ではなく、**複数回の推論実行中に発生する別種のOpenMP内部abort**である
  ことが分かった。

## 解釈 / 仮説

OpenMPのスレッドプールは遅延的に育つ実装が多く、1〜2回目の推論では
既存の(最小限の)スレッドで間に合っていたが、3回目以降で追加ワーカー
スレッドの新規生成(`pthread_create`)が必要になり、GarageBandの
sandbox下でそれが拒否され、OpenMPがこれを致命的エラーとして
`__kmp_abort_process`で全体を中断させた可能性が高いと判断した。
`OMP_NUM_THREADS=4`を要求していたため、複数スレッドを必要とする
コードパスに到達しやすかったと考えられる。

## 対応

`worker/rvc_worker.py`の`RVCStreamEngine.__init__`で、
`OMP_NUM_THREADS`のデフォルトを`"4"`から`"1"`へ変更した。1スレッドのみ
要求すれば追加のワーカースレッド生成自体が発生しないため、この種の
abortを回避できると判断した。`KMP_DUPLICATE_LIB_OK=TRUE`は保険として
残す。

トレードオフ: CPU推論が単一スレッドに制限されるため速度は低下する
(CLI再検証で1ブロックあたりの推論時間が約550ms→約1097ms(3ブロック平均)
に増加)。安定動作を優先した判断。

## 実行コマンド

```text
RVC_WORKER_SCRIPT=.../worker/rvc_worker.py build-macos/Debug/rvc-worker-smoke \
  /Users/saitoumitsuru/RVC-WebUI /Users/saitoumitsuru/RVC-WebUI/.venv/bin/python \
  /Users/saitoumitsuru/RVC-WebUI/assets/weights/deltamon_singing_40k_v2_20260901_e20.pth "" 130 80 3

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 結果

CLI再検証(3ブロック、以前クラッシュした呼び出し回数を上回る): 成功、
drop 0件。APP/AUとも再ビルド成功、auval成功。

## Recovery / 次の一手

GarageBand実機で再度確認する。今回もCLIでは再現できていないため
(sandbox固有の挙動)、この修正が実際に有効かは実機確認が必要。

## unknown

- GarageBand実機での修正後の動作(未検証、CLIでは元々再現しないため)
- OMP_NUM_THREADS=1固定によるCPU推論速度低下が実用上許容範囲か
