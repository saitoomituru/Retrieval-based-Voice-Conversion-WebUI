# 実験・開発ログ: deltamon-training-recovery

実施時刻: 2026-09-01T22:24+09:00
対象 Issue: #10、#11、#12、#13（関連: #6、#8）
branch / HEAD: issue-6-macos-runtime-bootstrap / adf33a394302480eb376695eb43b74ef89a2e212（作業中の未commit差分あり）
実行環境: macOS 15.7.7 / Intel x86_64 / Python 3.12.7 / CPU
結果: success（前処理・特徴量） / running（モデル学習）

## 目的

Terminal crashからデルタもん歌唱model作成を回収し、低火力CPUで再開する。

## 入力・設定

- AirDriveの公式学習音声をlocal symlink棚から読む。sourceはrepositoryにコピーしない。
- 実験名: `deltamon_singing_40k_v2_20260901`
- 40k / F0あり / v2 / single speaker / batch 1 / total epoch 20 / save every 5
- base: `assets/pretrained_v2/f0G40k.pth`, `f0D40k.pth`

## 実行した段階

```text
python -m train.preprocess logs/deltamon_source 40000 4 logs/deltamon_singing_40k_v2_20260901 False 3.0
python -m train.dataset.extract_f0 cpu logs/deltamon_singing_40k_v2_20260901 4 rmvpe
python -m train.dataset.extract_hubert_feature cpu 1 0 logs/deltamon_singing_40k_v2_20260901 v2 False
python -m train.train -e deltamon_singing_40k_v2_20260901 -sr 40k -f0 1 -bs 1 -te 20 -se 5 -pg assets/pretrained_v2/f0G40k.pth -pd assets/pretrained_v2/f0D40k.pth -l 1 -c 0 -sw 1 -v v2
```

## 観測事実

- 前処理: 65 files中、無音1本を明示skip、233 clips生成。
- F0: 233/233 success、failed 0。
- HuBERT: 233/233 success、failed 0、768次元feature。
- filelist: 完全な233 clips + mute 2行 = 235行。
- base generator/discriminator: `<All keys matched successfully>`。
- CPU学習はepoch 1へ入り、初期lossは`disc=4.069`, `gen=3.152`, `mel=25.525`, `kl=8.387`。
- 現在processは生存中でCPU約552%を使用して計算中。checkpointはepoch 5保存予定。

## 捕捉した失敗とRecovery

- 直接script起動: `ModuleNotFoundError: infer` → module entrypoint化。
- `PYTHONPATH`のみ: `train/train.py`の部分初期化循環import → `python -m`で回避。
- 無音clip: `UnboundLocalError(tmp_audio)` → 空sliceをskipし、sliceごとtailを書出し。
- GPUなしDDP: TCPStore bind `Operation not permitted` → CPU single-process化。
- CPU DataLoader worker: `torch_shm_manager ... Operation not permitted` → worker 0化。

## unknown

- 20 epochの完了時刻、音質、歌唱変換の実用性は未確認。
- Terminal crashの再現率は未確認。
- デルタもんmodelの公開・再配布・upstream先は本家確認待ち。
