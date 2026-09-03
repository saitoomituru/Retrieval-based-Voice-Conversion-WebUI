# 実験票: ロガー設計の欠陥を修正(issue #20)

実施時刻: 2026-09-03 11:12 JST (利用者観測), 記録は後追い
対象 Issue: #6, #20
branch / HEAD: gui-macos-portability
実行環境: macOS実機, GarageBand
結果: success (常時診断ログの実装まで。実機でのフリッカリング原因特定は継続)

## 目的

GarageBand実機で、ENGINEをONにするとSTATUS表示が「ERROR」と「ロード中」の
間で高速に点滅(フリッカリング)する問題が報告された。$TMPDIR配下のper-instance
ログ(instance_*.json/.process.log)を確認したが、直近の失敗に対応する
新規ファイルが見当たらなかった。

## 利用者からの指摘

per-instanceログはPython子processが実際にspawnされて初めて作られる。
`launchWorker()`が`writeWorkerConfig()`等、spawn**より前**の段階で
毎回失敗している場合、ログファイルが一切作られないため「新規ファイルが
ない」ことは「新規試行がない」ことを意味しない。これはロガー設計の
欠陥であり、観測できないことを確認者の見落としとして扱うべきではない、
との指摘を受けた。指摘は妥当と判断した。

## 対応

`WorkerClient_mac.cpp`の`setStatus()`に、常時有効な診断ログ
(`$TMPDIR/RVCRealtime/logs/diagnostic.log`、追記型)への書き込みを追加した。
- 全ての`setStatus()`呼び出し経路(config書き出し失敗、shm相当のファイル
  open/ftruncate/mmap失敗、workerスクリプト未検出、posix_spawn失敗、
  タイムアウト、worker異常終了等、既存の全エラーパス)を横断的に捕捉する
- 状態またはテキストが変化した場合のみ記録(同一状態の繰り返し記録による
  肥大化を防止)
- ミリ秒精度のタイムスタンプ付きで、プロセスがspawnされたか否かに関わらず
  記録される

## 実行コマンド

```text
cmake --build build-macos --target rvc-worker-smoke --config Debug
RVC_WORKER_SCRIPT=.../worker/rvc_worker.py build-macos/Debug/rvc-worker-smoke \
  /Users/saitoumitsuru/RVC-WebUI /Users/saitoumitsuru/RVC-WebUI/.venv/bin/python \
  /Users/saitoumitsuru/RVC-WebUI/assets/weights/deltamon_singing_40k_v2_20260901_e20.pth "" 130 80 1
cat "$TMPDIR/RVCRealtime/logs/diagnostic.log"

cmake --build build-macos --target RVCRealtime-app --config Debug
cmake --build build-macos --target RVCRealtime-au --config Debug
cp -R build-macos/out/Debug/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Rvcr Rvcp
```

## 結果

CLIで動作確認: `diagnostic.log`に状態遷移がミリ秒精度で正しく記録される
ことを確認(Loading RVC model → Loading Python runtime → Prewarming →
Ready)。APP/AU再ビルド・auval検証とも成功。

## Recovery / 次の一手

GarageBand実機で再度ENGINEをONにし、`diagnostic.log`の内容を確認する。
フリッカリングが再現すれば、今度こそspawn前の失敗を含めた正確な原因が
特定できるはず。

## unknown

- GarageBand実機でのフリッカリングの正確な原因(診断ログが実機で
  捕捉できるかも含め未検証)
