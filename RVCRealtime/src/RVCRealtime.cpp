#include "RVCRealtime.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "IControls.h"
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
#include "RuntimeControlClient.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

const IColor kBackground = IColor(255, 18, 20, 23);
const IColor kPanel = IColor(255, 33, 36, 39);
const IColor kHeaderColor = IColor(255, 235, 234, 228);
const IColor kText = IColor(255, 233, 232, 226);
const IColor kMuted = IColor(255, 152, 155, 155);
const IColor kAccent = IColor(255, 41, 151, 126);
const IColor kAmber = IColor(255, 230, 160, 48);

enum class PathRow { RvcRoot, Python, Model, Index };

#if defined(_WIN32)
std::wstring SettingsFilePath(const bool createDirectory)
{
  WDL_String directory;
  iplug::INIPath(directory, BUNDLE_NAME);
  const UTF8AsUTF16 directoryWide(directory.Get());
  if (createDirectory)
    CreateDirectoryW(directoryWide.Get(), nullptr);
  std::wstring result(directoryWide.Get());
  result += L"\\settings.ini";
  return result;
}

std::string ReadSetting(const wchar_t* key)
{
  const std::wstring settingsPath = SettingsFilePath(false);
  std::vector<wchar_t> value(32768, L'\0');
  GetPrivateProfileStringW(L"Paths", key, L"", value.data(), static_cast<DWORD>(value.size()), settingsPath.c_str());
  return UTF16AsUTF8(value.data()).Get();
}

void WriteSetting(const std::wstring& settingsPath, const wchar_t* key, const std::string& value)
{
  WritePrivateProfileStringW(L"Paths", key, UTF8AsUTF16(value.c_str()).Get(), settingsPath.c_str());
}
#else
std::filesystem::path SettingsFilePath(const bool createDirectory)
{
  WDL_String directory;
  iplug::INIPath(directory, BUNDLE_NAME);
  std::filesystem::path path(directory.Get());
  if (createDirectory) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  path /= "settings.ini";
  return path;
}

std::string ReadSetting(const char* key)
{
  std::ifstream file(SettingsFilePath(false));
  if (!file.is_open())
    return {};
  std::string line;
  const std::string prefix = std::string(key) + "=";
  while (std::getline(file, line)) {
    if (line.compare(0, prefix.size(), prefix) == 0)
      return line.substr(prefix.size());
  }
  return {};
}

void WriteSetting(std::ofstream& file, const char* key, const std::string& value)
{
  file << key << "=" << value << "\n";
}
#endif

bool PathIsFile(const std::string& path)
{
  if (path.empty())
    return false;
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(UTF8AsUTF16(path.c_str()).Get());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
#endif
}

bool PathIsDirectory(const std::string& path)
{
  if (path.empty())
    return false;
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(UTF8AsUTF16(path.c_str()).Get());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
#endif
}

std::string TrimTrailingSeparators(std::string path)
{
  while (path.size() > 3 && (path.back() == '\\' || path.back() == '/'))
    path.pop_back();
  return path;
}

std::string JoinPath(const std::string& root, const char* relative)
{
  if (root.empty())
    return {};
#if defined(_WIN32)
  std::string result = TrimTrailingSeparators(root);
  result += "\\";
  result += relative;
  return result;
#else
  std::filesystem::path path(TrimTrailingSeparators(root));
  path /= relative;
  return path.string();
#endif
}

std::string ParentDirectory(const std::string& filePath)
{
  const std::size_t separator = filePath.find_last_of("\\/");
  return separator == std::string::npos ? std::string() : filePath.substr(0, separator);
}

// Issue #15: the Windows RVC package bundles a portable interpreter at
// <root>/runtime/python.exe; the macOS dev setup in
// docs/macos-runtime-bootstrap.ja.md instead creates <root>/.venv. Neither guess
// applies on the other platform, so this branches instead of picking one path.
const char* PythonNotFoundMessage()
{
#if defined(_WIN32)
  return "runtime/python.exe not found; select Python manually.";
#else
  return ".venv/bin/python not found; select Python manually.";
#endif
}

std::string DetectPythonPath(const std::string& root)
{
#if defined(_WIN32)
  const std::string candidate = JoinPath(root, "runtime/python.exe");
#else
  const std::string candidate = JoinPath(root, ".venv/bin/python");
#endif
  return PathIsFile(candidate) ? candidate : std::string();
}

// Windows-only check (PE header via GetBinaryTypeW); RVC's runtime/python.exe
// convention doesn't exist outside Windows, so this only guards the Windows path.
bool Is64BitExecutable(const std::string& path)
{
#if defined(_WIN32)
  DWORD binaryType = 0;
  return GetBinaryTypeW(UTF8AsUTF16(path.c_str()).Get(), &binaryType) != FALSE
      && binaryType == SCS_64BIT_BINARY;
#else
  (void)path;
  return true;
#endif
}

IVStyle MakeStyle()
{
  return DEFAULT_STYLE
    .WithColor(kBG, kPanel)
    .WithColor(kFG, kAccent)
    .WithColor(kPR, kText)
    .WithColor(kFR, IColor(255, 64, 68, 72))
    .WithLabelText(IText(13.f, kMuted, "Roboto-Regular", EAlign::Center))
    .WithValueText(IText(14.f, kText, "Roboto-Regular", EAlign::Center))
    .WithRoundness(0.15f)
    .WithWidgetFrac(0.55f)
    .WithDrawShadows(false)
    .WithDrawFrame(false);
}

class RVCSliderControl final : public IVSliderControl
{
public:
  using IVSliderControl::IVSliderControl;

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    ISliderControlBase::OnMouseDown(x, y, mod);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod&) override
  {
    if (mStyle.showValue && mValueBounds.Contains(x, y))
      PromptUserInput(mValueBounds);
  }
};

const char* StatusName(const int status)
{
  switch (status) {
    case rvc::kStatusStarting: return "STARTING";
    case rvc::kStatusLoading: return "LOADING MODEL";
    case rvc::kStatusReady: return "READY";
    case rvc::kStatusError: return "ERROR";
    default: return "ENGINE OFF";
  }
}

// Issue #16: a small "\xE2\x96\xBE" (▾) button that scans one directory (non-recursive,
// matching webui.py's os.listdir(weight_root)/os.listdir(index_root) behavior) and
// shows a popup menu of matching files. Selecting one calls back into
// SetModelPath/SetIndexPath, same as the existing manual "..." browse button.
// Empty/missing directory just means the menu has 0 items and does nothing on click
// (never throws, never blocks) — manual browse remains available either way.
#if defined(__APPLE__)
class RVCFileMenuControl final : public IDirBrowseControlBase
{
public:
  using SelectionFunc = std::function<void(const char*)>;

  RVCFileMenuControl(const IRECT& bounds, const char* extension, SelectionFunc onSelect)
  : IDirBrowseControlBase(bounds, extension, /*showFileExtensions=*/true, /*scanRecursively=*/false)
  , mOnSelect(std::move(onSelect))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(kPanel, mRECT);
    g.DrawText(IText(14.f, kText, "Roboto-Regular", EAlign::Center), "\xE2\x96\xBE", mRECT);
  }

  void OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/) override
  {
    if (NItems() > 0)
      GetUI()->CreatePopupMenu(*this, mMainMenu, mRECT);
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* pMenu, int /*valIdx*/) override
  {
    if (pMenu) {
      if (auto* item = pMenu->GetChosenItem()) {
        mSelectedItemIndex = mItems.Find(item);
        CheckSelectedItem();
        WDL_String path;
        GetSelectedFile(path);
        if (path.GetLength() > 0 && mOnSelect)
          mOnSelect(path.Get());
      }
    }
    SetDirty(false);
  }

  // Re-points the scan at `directory` (e.g. after RVC ROOT changes). Safe to call
  // with a missing/empty directory: AddPath/SetupMenu just yield an empty menu.
  void Rescan(const char* directory)
  {
    ClearPathList();
    if (directory != nullptr && *directory != '\0')
      AddPath(directory, "");
    SetupMenu();
  }

private:
  SelectionFunc mOnSelect;
};
#endif

#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
class RVCRuntimeMenuControl final : public IControl
{
public:
  using SelectionFunc = std::function<void(const char*)>;

  explicit RVCRuntimeMenuControl(const IRECT& bounds, SelectionFunc onRuntimeChanged)
  : IControl(bounds)
  , mOnRuntimeChanged(std::move(onRuntimeChanged))
  {
  }

  void Draw(IGraphics& graphics) override
  {
    graphics.FillRect(kPanel, mRECT);
    const IRECT nameBounds(mRECT.L + 14.f, mRECT.T + 8.f, mRECT.R - 150.f, mRECT.T + 34.f);
    const IRECT detailBounds(mRECT.L + 14.f, mRECT.T + 36.f, mRECT.R - 14.f, mRECT.B - 6.f);
    const IRECT buttonBounds(mRECT.R - 138.f, mRECT.T + 8.f, mRECT.R - 8.f, mRECT.T + 36.f);
    graphics.DrawText(IText(13.f, kText, "Roboto-Regular", EAlign::Near), mDisplay.c_str(), nameBounds);
    graphics.DrawText(IText(10.f, kMuted, "Roboto-Regular", EAlign::Near), mDetail.c_str(), detailBounds);
    graphics.FillRect(kAccent, buttonBounds);
    graphics.DrawText(IText(11.f, kText, "Roboto-Regular"), "SCAN / SELECT \xE2\x96\xBE", buttonBounds);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    const rvc::RuntimeChoices result = mClient.list();
    if (!result.error.empty()) {
      mDisplay = "RUNTIME CONTROL OFFLINE";
      mDetail = result.error;
      SetDirty(false);
      return;
    }
    mChoices = result.choices;
    if (auto* model = GetUI()->GetControlWithTag(kCtrlModelName))
      model->As<ITextControl>()->SetStr(result.model.empty() ? "NOT CONFIGURED" : result.model.c_str());
    if (auto* index = GetUI()->GetControlWithTag(kCtrlIndexName))
      index->As<ITextControl>()->SetStr(result.index.empty() ? "NONE" : result.index.c_str());
    mMenu.Clear();
    for (size_t index = 0; index < mChoices.size(); ++index) {
      const bool selected = mChoices[index] == result.selected;
      mMenu.AddItem(mChoices[index].c_str(), -1,
                    selected ? IPopupMenu::Item::kChecked : IPopupMenu::Item::kNoFlags);
    }
    mDisplay = result.selected.empty() ? "SELECT RUNTIME ENGINE" : result.selected;
    mDetail = std::to_string(mChoices.size()) + " engine(s) detected by WebUI controller";
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu == nullptr || menu->GetChosenItem() == nullptr) {
      SetDirty(false);
      return;
    }
    const int index = menu->GetChosenItemIdx();
    if (index < 0 || index >= static_cast<int>(mChoices.size())) {
      SetDirty(false);
      return;
    }
    std::string error;
    if (mClient.select(mChoices[static_cast<size_t>(index)], error)) {
      mDisplay = mChoices[static_cast<size_t>(index)];
      mDetail = "Selected via 127.0.0.1:17864; new sessions use this engine";
      if (mOnRuntimeChanged)
        mOnRuntimeChanged("active");
    } else {
      mDisplay = "RUNTIME SELECT FAILED";
      mDetail = error;
    }
    SetDirty(false);
  }

private:
  rvc::RuntimeControlClient mClient;
  IPopupMenu mMenu {"RVC runtime engines"};
  std::vector<std::string> mChoices;
  std::string mDisplay {"CLICK SCAN / SELECT"};
  std::string mDetail {"WebUI owns Bonjour discovery; AU requests the shared list"};
  SelectionFunc mOnRuntimeChanged;
};

class RVCModelMenuControl final : public IControl
{
public:
  using SelectionFunc = std::function<void(const char*)>;

  RVCModelMenuControl(const IRECT& bounds, SelectionFunc onSelect)
  : IControl(bounds)
  , mOnSelect(std::move(onSelect))
  {
  }

  void Draw(IGraphics& graphics) override
  {
    graphics.FillRect(kAccent, mRECT);
    graphics.DrawText(IText(14.f, kText, "Roboto-Regular"), "\xE2\x96\xBE", mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    const rvc::RuntimeChoices result = mClient.list();
    if (!result.error.empty() || result.models.empty()) {
      if (auto* detail = GetUI()->GetControlWithTag(kCtrlStatusDetail))
        detail->As<ITextControl>()->SetStr(
          result.error.empty() ? "Selected runtime exposes no models" : result.error.c_str());
      SetDirty(false);
      return;
    }
    mModels = result.models;
    mMenu.Clear();
    for (const auto& model : mModels)
      mMenu.AddItem(model.name.c_str());
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu == nullptr || menu->GetChosenItem() == nullptr) {
      SetDirty(false);
      return;
    }
    const int index = menu->GetChosenItemIdx();
    if (index < 0 || index >= static_cast<int>(mModels.size())) {
      SetDirty(false);
      return;
    }
    const auto& model = mModels[static_cast<size_t>(index)];
    if (mOnSelect)
      mOnSelect(model.id.c_str());
    if (auto* label = GetUI()->GetControlWithTag(kCtrlModelName))
      label->As<ITextControl>()->SetStr(model.name.c_str());
    if (auto* label = GetUI()->GetControlWithTag(kCtrlIndexName))
      label->As<ITextControl>()->SetStr(model.index.empty() ? "NONE" : model.index.c_str());
    if (auto* detail = GetUI()->GetControlWithTag(kCtrlStatusDetail))
      detail->As<ITextControl>()->SetStr(
        model.id == "active" ? "AU follows the WebUI default model"
                             : "AU session model overrides the WebUI default");
    SetDirty(false);
  }

private:
  rvc::RuntimeControlClient mClient;
  IPopupMenu mMenu {"RVC runtime models"};
  std::vector<rvc::RuntimeModel> mModels;
  SelectionFunc mOnSelect;
};
#endif

} // namespace

RVCRealtime::RVCRealtime(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kEngine)->InitBool("Engine", false);
  GetParam(kPitch)->InitInt("Pitch", 12, -24, 24, "st");
  GetParam(kFormant)->InitDouble("Formant", 0.0, -12.0, 12.0, 0.01, "st");
  GetParam(kIndexRate)->InitDouble("Index", 0.0, 0.0, 1.0, 0.001);
  GetParam(kRmsMix)->InitDouble("RMS Mix", 0.5, 0.0, 1.0, 0.001);
  GetParam(kThreshold)->InitDouble("Gate", -60.0, -60.0, 0.0, 0.1, "dB");
  GetParam(kBlockMs)->InitInt("Block", 130, 20, 1000, "ms");
  GetParam(kCrossfadeMs)->InitInt("Crossfade", 80, 10, 100, "ms");
  GetParam(kExtraMs)->InitInt("Context", 2000, 500, 3000, "ms");
  GetParam(kF0Method)->InitEnum("F0 Method", 0, {"RMVPE", "FCPE", "PM"});
  GetParam(kDryWet)->InitDouble("Mix", 100.0, 0.0, 100.0, 0.1, "%");
  GetParam(kOutputGain)->InitDouble("Output", 0.0, -18.0, 12.0, 0.1, "dB");

  LoadUserConfiguration();
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
  // The localhost runtime owns filesystem paths. Keep the serialized field for
  // Windows/preset compatibility, but give a fresh macOS thin head the runtime's
  // stable default model id instead of requiring a local .pth path.
  const std::string storedModel = mModelPath.Get();
  if (storedModel.empty() || storedModel.find('/') != std::string::npos
      || storedModel.find('\\') != std::string::npos
      || (storedModel.size() > 4 && storedModel.substr(storedModel.size() - 4) == ".pth"))
    mModelPath.Set("active");
#endif
  if (mPythonPath.GetLength() == 0 && mRvcRoot.GetLength() > 0) {
    const std::string detected = DetectPythonPath(mRvcRoot.Get());
    if (!detected.empty())
      mPythonPath.Set(detected.c_str());
  }
  const std::string modelDirectory = ParentDirectory(mModelPath.Get());
  const std::string indexDirectory = ParentDirectory(mIndexPath.Get());
  mModelBrowseDirectory.Set(modelDirectory.empty() ? mRvcRoot.Get() : modelDirectory.c_str());
  mIndexBrowseDirectory.Set(indexDirectory.empty() ? mRvcRoot.Get() : indexDirectory.c_str());

  mWorker.setPath(rvc::kStateModelPath, mModelPath.Get());
  mWorker.setPath(rvc::kStateIndexPath, mIndexPath.Get());
  mWorker.setPath(rvc::kStateRvcRoot, mRvcRoot.Get());
  mWorker.setPath(rvc::kStatePythonPath, mPythonPath.Get());
  SyncParametersToWorker();

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                        GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* graphics) {
    graphics->EnableMouseOver(true);
    graphics->AttachCornerResizer(EUIResizerMode::Scale, false); // drag corner to scale whole UI
    graphics->AttachPanelBackground(kBackground);
    graphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    const IVStyle style = MakeStyle();
    const IRECT bounds = graphics->GetBounds();

    // Header
    graphics->AttachControl(new IPanelControl(bounds.GetFromTop(86.f), kHeaderColor));
    graphics->AttachControl(new ITextControl(IRECT(28, 10, 420, 52), "RVC REALTIME",
                                              IText(28.f, IColor(255, 23, 25, 27), "Roboto-Regular", EAlign::Near)));
    graphics->AttachControl(new ITextControl(IRECT(30, 50, 420, 76), "VOICE CONVERSION / CUDA BRIDGE",
                                              IText(12.f, IColor(255, 85, 89, 90), "Roboto-Regular", EAlign::Near)));
    graphics->AttachControl(new ITextControl(IRECT(560, 16, 750, 42), "ENGINE OFF",
                                              IText(14.f, IColor(255, 80, 84, 86), "Roboto-Regular", EAlign::Far)), kCtrlStatus);
    graphics->AttachControl(new ITextControl(IRECT(560, 42, 750, 66), "0 ms / 0 drop",
                                              IText(12.f, IColor(255, 100, 104, 105), "Roboto-Regular", EAlign::Far)), kCtrlPerformance);

    // Issue #33: host-owned render mode. This is deliberately read-only: an AU
    // cannot turn a realtime host render into an offline render by itself.
#if defined(__APPLE__)
    graphics->AttachControl(new IPanelControl(IRECT(430, 16, 555, 42), kPanel));
    graphics->AttachControl(new ITextControl(IRECT(430, 16, 555, 42), "REALTIME",
                                              IText(12.f, kMuted, "Roboto-Regular")), kCtrlRenderMode);
#endif

    // Runtime and model paths. MODEL/INDEX additionally get a "\xE2\x96\xBE" scan
    // button (issue #16: port webui.py's directory-scan model list) to the left of
    // the panel end, so the panel/text area is a little narrower on those two rows.
    auto attachFileRow = [&](const float y, const char* label, const int textTag, const PathRow pathRow) {
      const float rowHeight = 36.f;
      const bool hasMenu =
#if defined(__APPLE__)
        pathRow == PathRow::Model || pathRow == PathRow::Index;
#else
        false;
#endif
      const float panelRight = hasMenu ? 654.f : 692.f;
      graphics->AttachControl(new ITextControl(IRECT(30, y, 92, y + rowHeight), label,
                                                IText(13.f, kMuted, "Roboto-Regular", EAlign::Near)));
      graphics->AttachControl(new IPanelControl(IRECT(96, y, panelRight, y + rowHeight), kPanel));
      const char* initial = "";
      switch (pathRow) {
        case PathRow::RvcRoot: initial = mRvcRoot.Get(); break;
        case PathRow::Python: initial = mPythonPath.Get(); break;
        case PathRow::Model: initial = mModelPath.get_filepart(); break;
        case PathRow::Index: initial = mIndexPath.get_filepart(); break;
      }
      graphics->AttachControl(new ITextControl(IRECT(110, y, panelRight - 12.f, y + rowHeight), initial,
                                                IText(12.f, kText, "Roboto-Regular", EAlign::Near)), textTag);
#if defined(__APPLE__)
      if (pathRow == PathRow::Model) {
        graphics->AttachControl(new RVCFileMenuControl(IRECT(658, y, 694, y + rowHeight), "pth",
            [this](const char* path) { SetModelPath(path); }), kCtrlModelMenu);
      } else if (pathRow == PathRow::Index) {
        graphics->AttachControl(new RVCFileMenuControl(IRECT(658, y, 694, y + rowHeight), "index",
            [this](const char* path) { SetIndexPath(path); }), kCtrlIndexMenu);
      }
#endif
      auto action = [this, graphics, pathRow](IControl*) {
        switch (pathRow) {
          case PathRow::RvcRoot: ChooseRvcRoot(graphics); break;
          case PathRow::Python: ChoosePython(graphics); break;
          case PathRow::Model: ChooseModel(graphics); break;
          case PathRow::Index: ChooseIndex(graphics); break;
        }
      };
      graphics->AttachControl(new IVButtonControl(IRECT(700, y, 750, y + rowHeight), action, "...",
                                                  style.WithLabelText(IText(18.f, kText, "Roboto-Regular", EAlign::Center)), true));
    };
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
    graphics->AttachControl(new ITextControl(IRECT(30, 100, 92, 178), "RUNTIME",
                                              IText(13.f, kMuted, "Roboto-Regular", EAlign::Near)));
    graphics->AttachControl(new RVCRuntimeMenuControl(
      IRECT(96, 100, 750, 178), [this](const char* modelId) { SetModelPath(modelId); }),
      kCtrlRuntimeMenu);
    auto attachRuntimeOwnedRow = [&](const float y, const char* label, const int textTag,
                                     const char* initial) {
      const float rowHeight = 36.f;
      graphics->AttachControl(new ITextControl(IRECT(30, y, 140, y + rowHeight), label,
                                                IText(12.f, kMuted, "Roboto-Regular", EAlign::Near)));
      const bool modelRow = textTag == kCtrlModelName;
      const float panelRight = modelRow ? 700.f : 750.f;
      graphics->AttachControl(new IPanelControl(IRECT(144, y, panelRight, y + rowHeight), kPanel));
      graphics->AttachControl(new ITextControl(IRECT(158, y, panelRight - 12.f, y + rowHeight), initial,
                                                IText(12.f, kText, "Roboto-Regular", EAlign::Near)), textTag);
      if (modelRow) {
        graphics->AttachControl(new RVCModelMenuControl(
          IRECT(706, y, 750, y + rowHeight), [this](const char* modelId) { SetModelPath(modelId); }),
          kCtrlRuntimeModelMenu);
      }
    };
    attachRuntimeOwnedRow(184.f, "MODEL", kCtrlModelName, "SELECT RUNTIME MODEL");
    attachRuntimeOwnedRow(226.f, "INDEX", kCtrlIndexName, "MODEL-OWNED INDEX");
#else
    attachFileRow(100.f, "RVC ROOT", kCtrlRvcRoot, PathRow::RvcRoot);
    attachFileRow(142.f, "PYTHON", kCtrlPythonPath, PathRow::Python);
    attachFileRow(184.f, "MODEL", kCtrlModelName, PathRow::Model);
    attachFileRow(226.f, "INDEX", kCtrlIndexName, PathRow::Index);
#endif
    graphics->AttachControl(new ITextControl(IRECT(96, 266, 750, 290),
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
                                              "WebUI default; explicit AU model wins for this session",
#else
                                              "Select RVC root and Python runtime",
#endif
                                              IText(12.f, kAmber, "Roboto-Regular", EAlign::Near)), kCtrlStatusDetail);

    // 3x3 slider grid
    struct SliderLayout { int param; const char* label; };
    const SliderLayout sliders[] = {
      {kPitch, "PITCH"}, {kFormant, "FORMANT"}, {kIndexRate, "INDEX"},
      {kRmsMix, "RMS MIX"}, {kThreshold, "GATE"}, {kDryWet, "MIX"},
      {kBlockMs, "BLOCK"}, {kCrossfadeMs, "CROSSFADE"}, {kExtraMs, "CONTEXT"}
    };
    const float gridLeft = 30.f, gridTop = 300.f;
    const float cellWidth = 226.f, cellHeight = 62.f;
    const float gapX = 21.f, gapY = 10.f;
    for (int i = 0; i < 9; ++i) {
      const int column = i % 3;
      const int row = i / 3;
      const float x = gridLeft + column * (cellWidth + gapX);
      const float y = gridTop + row * (cellHeight + gapY);
      const IRECT cell(x, y, x + cellWidth, y + cellHeight);
      graphics->AttachControl(new IPanelControl(cell, kPanel));
      graphics->AttachControl(new RVCSliderControl(cell.GetPadded(-8.f), sliders[i].param,
                                                   sliders[i].label, style, true, EDirection::Horizontal));
    }

    // Bottom row: F0 method, output gain, engine toggle — one shared baseline
    const float bottomY = 566.f, bottomHeight = 44.f;
    graphics->AttachControl(new ITextControl(IRECT(30, bottomY, 100, bottomY + bottomHeight), "F0 METHOD",
                                              IText(12.f, kMuted, "Roboto-Regular", EAlign::Near)));
    graphics->AttachControl(new IVMenuButtonControl(IRECT(104, bottomY, 236, bottomY + bottomHeight), kF0Method,
                                                    "", style));
    graphics->AttachControl(new IPanelControl(IRECT(258, bottomY, 560, bottomY + bottomHeight), kPanel));
    graphics->AttachControl(new RVCSliderControl(IRECT(266, bottomY + 2, 552, bottomY + bottomHeight - 2), kOutputGain,
                                                 "OUTPUT", style, true, EDirection::Horizontal));
    graphics->AttachControl(new IVToggleControl(IRECT(600, bottomY, 750, bottomY + bottomHeight), kEngine,
                                                "ENGINE", style));
  };
#endif
}

#if IPLUG_DSP
void RVCRealtime::OnReset()
{
  const double sampleRate = GetSampleRate() > 0.0 ? GetSampleRate() : 48000.0;
  const int blockSize = std::max(1, GetBlockSize());
  mWorker.setSampleRate(sampleRate);
  ResizeBuffers(blockSize, sampleRate);
  SyncParametersToWorker();
  const int latency = CalculateLatencyFrames(GetParam(kBlockMs)->Value());
  mTargetDelayFrames.store(latency, std::memory_order_relaxed);
  SetLatency(latency);
}

void RVCRealtime::OnParamChange(const int paramIdx)
{
  if (paramIdx < 0 || paramIdx >= kNumParams)
    return;
  double value = GetParam(paramIdx)->Value();
  if (paramIdx == kDryWet)
    value /= 100.0;
  mWorker.setParameter(static_cast<rvc::ParameterId>(paramIdx), static_cast<float>(value));
  if (paramIdx == kEngine) {
    if (value >= 0.5) {
      std::string error;
      if (!ValidateConfiguration(error)) {
        {
          std::lock_guard<std::mutex> lock(mStateMutex);
          mValidationMessage.Set(error.c_str());
        }
        GetParam(kEngine)->Set(0.0);
        SendParameterValueFromAPI(kEngine, 0.0, false);
        mWorker.setEnabled(false);
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mValidationMessage.Set("");
      }
      SaveUserConfiguration();
      mWorker.setEnabled(true);
    } else {
      mWorker.setEnabled(false);
    }
  }
  if (paramIdx == kBlockMs) {
    const int latency = CalculateLatencyFrames(value);
    mTargetDelayFrames.store(latency, std::memory_order_relaxed);
    SetLatency(latency);
  }
}

void RVCRealtime::ProcessBlock(sample** inputs, sample** outputs, const int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();
  if (nFrames <= 0 || nOut <= 0)
    return;

  if (nIn <= 0) {
    for (int channel = 0; channel < nOut; ++channel)
      std::memset(outputs[channel], 0, static_cast<size_t>(nFrames) * sizeof(sample));
    return;
  }

  // Mono-safe passthrough: never leave output buffers unwritten.
  auto passthrough = [&]() {
    for (int channel = 0; channel < nOut; ++channel) {
      const sample* source = inputs[std::min(channel, nIn - 1)];
      if (outputs[channel] != source)
        std::memcpy(outputs[channel], source, static_cast<size_t>(nFrames) * sizeof(sample));
    }
  };

  if (nFrames > static_cast<int>(mMonoInput.size())) {
    passthrough();
    return;
  }

  const bool requested = GetParam(kEngine)->Bool();
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
  const bool renderingOffline = GetRenderingOffline();
  mWorker.setRenderingOffline(renderingOffline);
#else
  const bool renderingOffline = false;
#endif
  if (!requested || (!renderingOffline && !mWorker.isReady())) {
    mActiveBlend = std::max(0.0f, mActiveBlend - static_cast<float>(nFrames) / 1024.0f);
    passthrough();
    return;
  }

  const sample* inL = inputs[0];
  const sample* inR = inputs[nIn > 1 ? 1 : 0];
  sample* outL = outputs[0];
  sample* outR = nOut > 1 ? outputs[1] : nullptr;

  for (int frame = 0; frame < nFrames; ++frame)
    mMonoInput[static_cast<size_t>(frame)] = static_cast<float>(0.5 * (inL[frame] + inR[frame]));
  size_t wetRead = 0;
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
  if (renderingOffline) {
    if (mWorker.processOffline(mMonoInput.data(), static_cast<size_t>(nFrames),
                               mWetOutput.data(), static_cast<size_t>(nFrames)))
      wetRead = static_cast<size_t>(nFrames);
  } else
#endif
  {
    mWorker.pushInput(mMonoInput.data(), static_cast<size_t>(nFrames));
    wetRead = mWorker.popOutput(mWetOutput.data(), static_cast<size_t>(nFrames));
  }
  std::fill(mWetOutput.begin() + static_cast<ptrdiff_t>(wetRead),
            mWetOutput.begin() + nFrames, 0.0f);

  const float mix = static_cast<float>(GetParam(kDryWet)->Value() / 100.0);
  const float gain = static_cast<float>(std::pow(10.0, GetParam(kOutputGain)->Value() / 20.0));
  const int maxDelay = static_cast<int>(mDryDelay.size() / 2);
  const int delay = std::clamp(mTargetDelayFrames.load(std::memory_order_relaxed), 0, maxDelay - 1);
  const int readPosition = (mDryWritePosition + maxDelay - delay) % maxDelay;
  mActiveBlend = std::min(1.0f, mActiveBlend + static_cast<float>(nFrames) / 2048.0f);

  for (int frame = 0; frame < nFrames; ++frame) {
    const int write = (mDryWritePosition + frame) % maxDelay;
    const int read = (readPosition + frame) % maxDelay;
    mDryDelay[static_cast<size_t>(write) * 2] = static_cast<float>(inL[frame]);
    mDryDelay[static_cast<size_t>(write) * 2 + 1] = static_cast<float>(inR[frame]);
    const float wet = mWetOutput[static_cast<size_t>(frame)] * gain;
    const float processedL = mDryDelay[static_cast<size_t>(read) * 2] * (1.0f - mix) + wet * mix;
    const float processedR = mDryDelay[static_cast<size_t>(read) * 2 + 1] * (1.0f - mix) + wet * mix;
    outL[frame] = static_cast<sample>(inL[frame] * (1.0f - mActiveBlend) + processedL * mActiveBlend);
    if (outR)
      outR[frame] = static_cast<sample>(inR[frame] * (1.0f - mActiveBlend) + processedR * mActiveBlend);
  }
  mDryWritePosition = (mDryWritePosition + nFrames) % maxDelay;

  // Copy to any extra connected outputs beyond stereo.
  for (int channel = 2; channel < nOut; ++channel)
    std::memcpy(outputs[channel], outputs[channel & 1], static_cast<size_t>(nFrames) * sizeof(sample));
}
#endif

void RVCRealtime::OnIdle()
{
#if IPLUG_EDITOR
  if (GetUI() == nullptr)
    return;
  std::string validationMessage;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    validationMessage = mValidationMessage.Get();
  }
  if (auto* status = GetUI()->GetControlWithTag(kCtrlStatus))
    status->As<ITextControl>()->SetStr(validationMessage.empty() ? StatusName(mWorker.status()) : "CONFIG ERROR");
  if (auto* detail = GetUI()->GetControlWithTag(kCtrlStatusDetail))
    detail->As<ITextControl>()->SetStr(validationMessage.empty() ? mWorker.statusText().c_str() : validationMessage.c_str());
  if (auto* performance = GetUI()->GetControlWithTag(kCtrlPerformance)) {
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
    // GarageBand may suspend editor repainting while it renders. Keep a
    // process-lifetime latch so the human gate can still verify afterwards
    // that the host actually asserted the standard offline property.
    performance->As<ITextControl>()->SetStrFmt(80, "%.0f ms / %.0f drop / %u off",
                                               mWorker.inferMs(), mWorker.droppedBlocks(),
                                               mWorker.offlineRenderCount());
#else
    performance->As<ITextControl>()->SetStrFmt(80, "%.0f ms / %.0f drop", mWorker.inferMs(), mWorker.droppedBlocks());
#endif
  }
#if defined(__APPLE__)
  if (auto* renderMode = GetUI()->GetControlWithTag(kCtrlRenderMode))
    renderMode->As<ITextControl>()->SetStr(GetRenderingOffline() ? "OFFLINE" : "REALTIME");
#endif
#endif
}

void RVCRealtime::OnUIOpen()
{
  Plugin::OnUIOpen(); // pushes current param values to the UI controls
  UpdateFileLabels();
}

bool RVCRealtime::SerializeState(IByteChunk& chunk) const
{
  std::lock_guard<std::mutex> lock(mStateMutex);
  chunk.PutStr(mModelPath.Get());
  chunk.PutStr(mIndexPath.Get());
  chunk.PutStr(mRvcRoot.Get());
  chunk.PutStr(mPythonPath.Get());
  return SerializeParams(chunk);
}

int RVCRealtime::UnserializeState(const IByteChunk& chunk, int startPos)
{
  WDL_String model, index, root, python;
  startPos = chunk.GetStr(model, startPos);
  startPos = chunk.GetStr(index, startPos);
  startPos = chunk.GetStr(root, startPos);
  startPos = chunk.GetStr(python, startPos);
  if (startPos < 0)
    return startPos;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mModelPath.Set(model.Get());
    mIndexPath.Set(index.Get());
    mRvcRoot.Set(root.Get());
    mPythonPath.Set(python.Get());
  }
  mWorker.setPath(rvc::kStateModelPath, model.Get());
  mWorker.setPath(rvc::kStateIndexPath, index.Get());
  mWorker.setPath(rvc::kStateRvcRoot, root.Get());
  mWorker.setPath(rvc::kStatePythonPath, python.Get());
  startPos = UnserializeParams(chunk, startPos);
  SyncParametersToWorker();
  UpdateFileLabels();
  return startPos;
}

void RVCRealtime::SyncParametersToWorker()
{
  for (int i = 0; i < kNumParams; ++i)
    OnParamChange(i);
}

int RVCRealtime::CalculateLatencyFrames(const double blockMs) const
{
  const double sampleRate = GetSampleRate() > 0.0 ? GetSampleRate() : 48000.0;
  const double zc = std::max(1.0, std::floor(sampleRate / 100.0));
  const int block = static_cast<int>(std::round(blockMs / 1000.0 * sampleRate / zc) * zc);
  return block * 2;
}

void RVCRealtime::ResizeBuffers(const int blockSize, const double sampleRate)
{
  const size_t scratch = static_cast<size_t>(std::max(blockSize, 131072));
  mMonoInput.assign(scratch, 0.0f);
  mWetOutput.assign(scratch, 0.0f);
  mDryDelay.assign(static_cast<size_t>(std::max(1.0, sampleRate * 4.0)) * 2, 0.0f);
  mDryWritePosition = 0;
  mActiveBlend = 0.0f;
}

void RVCRealtime::ChooseRvcRoot(IGraphics* graphics)
{
  WDL_String directory(mRvcRoot.Get());
  graphics->PromptForDirectory(directory, [this](const WDL_String&, const WDL_String& selectedDirectory) {
    if (selectedDirectory.GetLength() > 0)
      SetRvcRoot(selectedDirectory.Get());
  });
}

void RVCRealtime::ChoosePython(IGraphics* graphics)
{
  WDL_String fileName, path;
  const std::string currentDirectory = ParentDirectory(mPythonPath.Get());
  if (!currentDirectory.empty())
    path.Set(currentDirectory.c_str());
  else if (mRvcRoot.GetLength() > 0)
    path.Set(JoinPath(mRvcRoot.Get(), "runtime").c_str());
  graphics->PromptForFile(fileName, path, EFileAction::Open, "exe",
                          [this](const WDL_String& selected, const WDL_String&) {
    if (selected.GetLength() > 0)
      SetPythonPath(selected.Get());
  });
}

void RVCRealtime::ChooseModel(IGraphics* graphics)
{
  WDL_String fileName, path(mModelBrowseDirectory.Get());
  if (path.GetLength() == 0)
    path.Set(mRvcRoot.Get());
  graphics->PromptForFile(fileName, path, EFileAction::Open, "pth",
                          [this](const WDL_String& selected, const WDL_String& selectedDirectory) {
    if (selected.GetLength() > 0) {
      mModelBrowseDirectory.Set(selectedDirectory.Get());
      SetModelPath(selected.Get());
    }
  });
}

void RVCRealtime::ChooseIndex(IGraphics* graphics)
{
  WDL_String fileName, path(mIndexBrowseDirectory.Get());
  if (path.GetLength() == 0)
    path.Set(mRvcRoot.Get());
  graphics->PromptForFile(fileName, path, EFileAction::Open, "index",
                          [this](const WDL_String& selected, const WDL_String& selectedDirectory) {
    if (selected.GetLength() > 0) {
      mIndexBrowseDirectory.Set(selectedDirectory.Get());
      SetIndexPath(selected.Get());
    }
  });
}

void RVCRealtime::SetRvcRoot(const char* path)
{
  const std::string root = TrimTrailingSeparators(path != nullptr ? path : "");
  const std::string detectedPython = DetectPythonPath(root);
  const bool pythonDetected = !detectedPython.empty();
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mRvcRoot.Set(root.c_str());
    mPythonPath.Set(pythonDetected ? detectedPython.c_str() : "");
    mModelBrowseDirectory.Set(root.c_str());
    mIndexBrowseDirectory.Set(root.c_str());
    mValidationMessage.Set(pythonDetected ? "" : PythonNotFoundMessage());
  }
  mWorker.setPath(rvc::kStateRvcRoot, root.c_str());
  mWorker.setPath(rvc::kStatePythonPath, pythonDetected ? detectedPython.c_str() : "");
  StopEngineForPathChange();
  UpdateFileLabels();
}

void RVCRealtime::SetPythonPath(const char* path)
{
  const char* value = path != nullptr ? path : "";
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mPythonPath.Set(value);
    mValidationMessage.Set("");
  }
  mWorker.setPath(rvc::kStatePythonPath, value);
  StopEngineForPathChange();
  UpdateFileLabels();
}

void RVCRealtime::SetModelPath(const char* path)
{
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mModelPath.Set(path);
  }
  mWorker.setPath(rvc::kStateModelPath, path);
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
  // The management thread reconnects and sends the opaque id in SESSION_OPEN.
  // Keep the host parameter enabled; no filesystem or network work happens on
  // the audio callback.
#else
  StopEngineForPathChange();
#endif
  UpdateFileLabels();
}

void RVCRealtime::SetIndexPath(const char* path)
{
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mIndexPath.Set(path);
  }
  mWorker.setPath(rvc::kStateIndexPath, path);
  StopEngineForPathChange();
  UpdateFileLabels();
}

void RVCRealtime::StopEngineForPathChange()
{
  mWorker.setEnabled(false);
  if (GetParam(kEngine)->Bool()) {
    GetParam(kEngine)->Set(0.0);
    SendParameterValueFromAPI(kEngine, 0.0, false);
  }
}

bool RVCRealtime::ValidateConfiguration(std::string& error) const
{
  std::string root, python, model, index;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    root = mRvcRoot.Get();
    python = mPythonPath.Get();
    model = mModelPath.Get();
    index = mIndexPath.Get();
  }
#if defined(__APPLE__) && !defined(RVC_MAC_LEGACY_EMBEDDED_WORKER)
  (void) root;
  (void) python;
  (void) index;
  if (model.empty()) { error = "Select a runtime model id."; return false; }
  error.clear();
  return true;
#else
  if (root.empty()) { error = "Select the RVC package root."; return false; }
  if (!PathIsDirectory(root)) { error = "RVC root folder does not exist."; return false; }
  if (!PathIsFile(JoinPath(root, "infer/rtrvc.py"))) { error = "RVC source not found: infer/rtrvc.py."; return false; }
  if (!PathIsFile(JoinPath(root, "configs/config.py"))) { error = "RVC source not found: configs/config.py."; return false; }
  if (python.empty()) { error = PythonNotFoundMessage(); return false; }
  if (!PathIsFile(python)) { error = "Selected Python executable does not exist."; return false; }
  if (!Is64BitExecutable(python)) { error = "Selected Python must be a 64-bit executable."; return false; }
  if (model.empty()) { error = "Select an RVC .pth model."; return false; }
  if (!PathIsFile(model)) { error = "Selected model file does not exist."; return false; }
  if (!index.empty() && !PathIsFile(index)) { error = "Selected index file does not exist."; return false; }
  error.clear();
  return true;
#endif
}

void RVCRealtime::LoadUserConfiguration()
{
#if defined(_WIN32)
  const std::string root = ReadSetting(L"RvcRoot");
  const std::string python = ReadSetting(L"PythonPath");
  const std::string model = ReadSetting(L"ModelPath");
  const std::string index = ReadSetting(L"IndexPath");
#else
  const std::string root = ReadSetting("RvcRoot");
  const std::string python = ReadSetting("PythonPath");
  const std::string model = ReadSetting("ModelPath");
  const std::string index = ReadSetting("IndexPath");
#endif
  if (!root.empty()) mRvcRoot.Set(root.c_str());
  if (!python.empty()) mPythonPath.Set(python.c_str());
  if (!model.empty()) mModelPath.Set(model.c_str());
  if (!index.empty()) mIndexPath.Set(index.c_str());
}

void RVCRealtime::SaveUserConfiguration() const
{
  std::string root, python, model, index;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    root = mRvcRoot.Get();
    python = mPythonPath.Get();
    model = mModelPath.Get();
    index = mIndexPath.Get();
  }
#if defined(_WIN32)
  const std::wstring settingsPath = SettingsFilePath(true);
  WriteSetting(settingsPath, L"RvcRoot", root);
  WriteSetting(settingsPath, L"PythonPath", python);
  WriteSetting(settingsPath, L"ModelPath", model);
  WriteSetting(settingsPath, L"IndexPath", index);
#else
  std::ofstream file(SettingsFilePath(true), std::ios::trunc);
  if (!file.is_open())
    return;
  WriteSetting(file, "RvcRoot", root);
  WriteSetting(file, "PythonPath", python);
  WriteSetting(file, "ModelPath", model);
  WriteSetting(file, "IndexPath", index);
#endif
}

void RVCRealtime::UpdateFileLabels()
{
#if IPLUG_EDITOR
  if (GetUI() == nullptr)
    return;
  std::lock_guard<std::mutex> lock(mStateMutex);
  if (auto* root = GetUI()->GetControlWithTag(kCtrlRvcRoot))
    root->As<ITextControl>()->SetStr(mRvcRoot.Get());
  if (auto* python = GetUI()->GetControlWithTag(kCtrlPythonPath))
    python->As<ITextControl>()->SetStr(mPythonPath.Get());
  if (auto* model = GetUI()->GetControlWithTag(kCtrlModelName))
    model->As<ITextControl>()->SetStr(mModelPath.get_filepart());
  if (auto* index = GetUI()->GetControlWithTag(kCtrlIndexName))
    index->As<ITextControl>()->SetStr(mIndexPath.get_filepart());
#if defined(__APPLE__)
  RescanFileMenus(); // caller (here) already holds mStateMutex; see declaration comment
#endif
#endif
}

// Issue #16: re-point the MODEL/INDEX scan menus at <rvcRoot>/assets/weights and
// <rvcRoot>/assets/indices (the same relative layout webui.py's weight_root/
// outside_index_root scan) whenever a tracked path changes. Only ever called from
// UpdateFileLabels(), which already holds mStateMutex while reading mRvcRoot here.
#if defined(__APPLE__)
void RVCRealtime::RescanFileMenus()
{
#if IPLUG_EDITOR
  const std::string weightsDir = JoinPath(mRvcRoot.Get(), "assets/weights");
  const std::string indicesDir = JoinPath(mRvcRoot.Get(), "assets/indices");
  if (auto* modelMenu = GetUI()->GetControlWithTag(kCtrlModelMenu))
    modelMenu->As<RVCFileMenuControl>()->Rescan(weightsDir.c_str());
  if (auto* indexMenu = GetUI()->GetControlWithTag(kCtrlIndexMenu))
    indexMenu->As<RVCFileMenuControl>()->Rescan(indicesDir.c_str());
#endif
}
#endif
