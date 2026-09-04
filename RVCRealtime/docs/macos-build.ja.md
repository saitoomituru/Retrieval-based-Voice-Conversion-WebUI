# macOS向けRVCRealtimeビルド手順

状態: `[APP/AU RELEASE BUILD確認済み]` `[AU実機変換・OFFLINE BOUNCE確認済み]`

この文書は、`RVCRealtime`をmacOS上でconfigure・compileするための手順である。
macOS AUは`WorkerClient_stream_mac.cpp`を使う薄いaudio headであり、Pythonやmodelを
GarageBand内で起動しない。WebUI所有runtimeへlocalhost RSVCで接続する。Windows VSTは
既存の`WorkerClient_win.cpp`と共有メモリworkerを維持する。

## toolchain要件

```text
確認環境: macOS 15.2, Xcode 16.2, cmake 3.31.3, x86_64
```

- Xcode Command Line Tools（`xcode-select --install`）
- cmake 3.14以上（`brew install cmake`等）
- ネットワーク到達性（`third_party/iPlug2` submoduleとiPlug2公式の
  prebuilt依存zipをGitHubから取得するため）

Apple Silicon実機での動作は未確認。今回の確認はすべてIntel x86_64で行った。

## ビルド

```zsh
scripts/build-macos.sh
```

初回実行時、`third_party/iPlug2` submoduleが未取得なら自動でfetchし、iPlug2の
prebuilt依存（IGraphics/NanoVG等）も未取得なら`Dependencies/download-prebuilt-libs.sh mac`を
自動実行する。2回目以降はこれらをスキップし、`cmake --build`のみ実行する。

デフォルトでは`RVCRealtime-app`（スタンドアロン、DAW不要でGUI確認できる）と
`RVCRealtime-au`（AUv2 `.component`）の両方をDebug構成でbuildする。

```zsh
scripts/build-macos.sh --config Release
scripts/build-macos.sh --targets "RVCRealtime-app"
```

成果物は`build-macos/out/<Config>/`以下に生成される。

## AUの手動インストールと検証

`IPLUG_DEPLOY_PLUGINS OFF`のため、AUは自動配置されない。ローカル確認用に手動でcopyする。

```zsh
cp -R build-macos/out/Release/RVCRealtime.component ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar 2>/dev/null
auval -v aufx Rvcr Rvcp
```

## VST2/VST3について

この実装ではmacOS向けにVST2/VST3をbuildしない（`CMakeLists.txt`の
`RVC_FORMATS_DEFAULT`はWindows: `VST2 VST3`、macOS: `APP AU`）。Windows側の
挙動・ビルド手順（`scripts/build.ps1`）はこの変更で変えていない。

## 既知の制限

- WebUIを先に手動起動する。AUからWebUIをLaunchServices起動する機能は未実装
- Intel CPUのrealtime推論はdeadline超過によるdropoutがある。GarageBand標準offline bounceは実機聴感合格
- Apple Silicon実機未確認
- Logic Pro実機未確認（GarageBandのみ確認済み）
