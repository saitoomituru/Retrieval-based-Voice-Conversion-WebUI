# GarageBand文書読込時ImageIOクラッシュの責務分離

対象Issue: #29（Human Gateを妨げたhost側障害。RVC defectとしては未確定）

対象branch / HEAD: `main` / `29fa88f`

実行環境:

- macOS 15.7.7 / Intel x86_64
- GarageBand 10.4.14 (6648)
- RVCRealtime Audio Unit: user component directoryへ配備済み

## 目的

GarageBand再起動時の`EXC_BAD_ACCESS`がRVCRealtime AUの変更に起因するか、
GarageBand・project・ImageIO側の別障害かを、crash reportの実行経路とロード済み
binaryから分離する。元projectは変更しない。

## 入力

- crash report: GarageBand PID 36330、2026-09-04 19:53:53 +0900
- project: `/Users/saitoumitsuru/Music/GarageBand/村祭り.band`
- GarageBand設定`MANPDSelectedTemplatePath`も同projectを指していた

## 観測事実

1. main threadは`NSDocument initWithContentsOfURL`からImageIOへ入り、
   `SetupTIFFErrorHandler()`内で`0x000000000bad4007`を実行しようとして
   `EXC_BAD_ACCESS (SIGSEGV)`で終了した。
2. crash reportのstackとBinary Imagesに`RVCRealtime`、`org.RVCProject`、
   RVC worker/runtimeは存在しない。RVCRealtime AUのaudio callback、GUI、network
   threadへ到達した証拠もない。
3. RVCRealtime binaryの`otool -L`はApple system frameworkとsystem libraryだけを
  示し、libtiff/libpng/libjpeg等および`/usr/local`へ直接依存しない。
4. 一方、crashしたGarageBand processにはHomebrewの
   `libtiff.6.dylib`、`libpng16.16.dylib`、`libjpeg.8.3.2.dylib`、
   `libgif.7.2.0.dylib`、`liblzma.5.dylib`、`libzstd.1.5.7.dylib`が
   ロードされていた。crash位置はその中のTIFF reader初期化経路である。
5. project内で`file`が画像として検出したのは
   `Alternatives/000/WindowImage.jpg`だけだった。JPEG本体にExif/TIFF metadataが
   あり、`sips`では読めたため、単純な読取不能ファイルとは確認できない。
6. 元projectは変更せず、APFS clone
   `/private/tmp/garageband-rvc-human.aNb62d/村祭り.band`を作った。
   cloneだけで`WindowImage.jpg`を退避しても、GarageBand起動はsignal 11で終了した。
   ただし自動再オープンが元projectへ先行した可能性を排除できないため、画像破損説を
   完全否定する試験にはならない。
7. 調査時点ではWebUI control port `127.0.0.1:17864`も停止していた。これは
   GarageBand crash stackとは別であり、Human Gate再開前にrunner再起動が必要である。

## 解釈

`RVCRealtime AUが今回の直接原因`は**棄却**する。crashはAUのロード・音声処理より前の
GarageBand document/ImageIO経路で発生している。

有力仮説は、GarageBand 10.4.14とmacOS 15.7.7のImageIO TIFF初期化時に、
`/usr/local`のHomebrew画像codecが同一processへ解決されたことによるsymbol/ABI衝突で
ある。ただし、どのGarageBand frameworkまたは動的ロードがHomebrew libraryを導入した
かはcrash reportだけでは確定できず、`host/image-loader hypothesis`として保持する。

project側のExif/TIFF metadataが発火入力である可能性も残るが、元ファイルの破損を
確認した事実はない。

## 非破壊Recovery

AppleのGarageBand診断順に沿い、次の順でHuman Gateを再開する。

1. Dock/FinderからGarageBandを手動起動し、起動直後からControlを押してaudio
   input/outputを起動しない。空projectが開けるか確認する。
2. 空projectが開くなら、元projectを直接上書きせず複製を開く。
3. それでも文書読込で落ちる場合は、GarageBand設定を控えた後、Apple案内の
   `defaults delete com.apple.garageband10`を使って設定を初期化し、Macを再起動する。
   この操作はcustom preferenceを失うため、この実験では実行していない。
4. Audio Units無効化でprojectが開くかを対照試験にする。開かなければRVCを含むAU説は
   さらに弱まり、project/ImageIO側として扱う。
5. `/usr/local/lib`やHomebrew Cellarは他の研究環境へ影響するため、無断削除・unlink・
   downgradeをしない。必要なら別user accountで同projectの複製を開く対照試験を先に行う。

## 結果

`host-failure / RVC-direct-cause-not-supported / workaround-proposed / human-retest-required`

RVC repositoryのsource修正対象は検出しなかったため、クラッシュに合わせたAU code変更は
行わない。Human GateはGarageBandを手動で開けた後、runtimeを再起動して再開する。

## unknown / 再開条件

- `0x0bad4007`へ解決された具体的なsymbolとHomebrew codec導入元
- GarageBand fresh launch、空project、Audio Units無効の各対照結果
- 元project複製を自動再オープンなしで開いた結果
- 修正版runtimeに対するLocalhost / Bonjour self / offline bounceの人間聴感

