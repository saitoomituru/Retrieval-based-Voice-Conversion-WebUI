# WebUI SIGTERM live-host fault-injection

- 対象 Issue: #36
- 対象 branch: `main`
- 修正 commit: `4da7b00`
- 実行環境: Intel Mac Pro、macOS 15.7.7、Python 3.12.7、Gradio 3系
- 実行者: Codex
- 日時: 2026-09-04 20:47–20:50 JST
- 結果: `success`

## 目的

DAWとは独立したWebUI processへ意図的にSIGTERMを注入し、WebUIが所有するrunner、gateway/control listener、Bonjour advertiser/browserがbounded time内に回収されることを確認する。偶発終了待ちではなく、稼働中hostへの障害注入試験である。

## 変更

Gradio 3系のmain-thread待機は`KeyboardInterrupt`でserverを閉じる一方、SIGTERMは既定終了のため外側の`finally`へ到達しなかった。signal handler内ではI/O、join、child停止を行わず、SIGTERMを`KeyboardInterrupt`へ変換する。既存のGradio終了処理とWebUIの`finally`が実際の停止を所有する。

## ベースライン再現

変更前binaryで稼働していたWebUI PID 35708だけへSIGTERMを送った。

- WebUI PID 35708: 終了
- runner PID 35732: PPID 1で残留、`*:17866`をLISTEN
- dns-sd PID 35733 / 35734: PPID 1で残留

PID、PPID、commandを再確認後、上記の孤児と以前の同障害由来PID 16993 / 16994だけを停止した。GarageBandとport 7867の別WebUI PID 1301にはsignalを送っていない。

## 修正版への障害注入

1. commit `4da7b00`のWebUIをport 7865で起動した。
2. WebUI PID 47740、所有runner PID 47749、dns-sd PID 47751 / 47752をPPIDとcommandで確認した。
3. listener `7865 / 17864 / 17865 / 17866`を確認した。
4. WebUI PID 47740だけへSIGTERMを送った。
5. Gradioが`Keyboard interruption in main thread... closing server.`を出力した。
6. 4 PIDと4 listenerがすべて消えたことを確認した。

## 機械試験

- `tests.test_rvc_runtime_lifecycle`: handler復元、SIGTERMから外側`finally`到達
- `tests.test_rvc_runtime_supervisor`: 所有childだけ停止、既存runner再利用を含む
- 合計8/8 success

## 境界 / UNKNOWN

- SIGKILLはprocess内handlerを実行できないため対象外である。
- launchd等の外部supervisorを採用する配布形態は別設計である。
- 本試験はWebUI→child方向の終了であり、runner先行障害からの再生成試験とは別の受入点である。
