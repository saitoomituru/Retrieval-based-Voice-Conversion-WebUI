#include "WorkerClient.hpp"
#include "config.h"

// macOS IPC bridge (issue #3/#6). Wire protocol (header layout, ring buffer
// framing) is identical to WorkerClient_win.cpp / worker/rvc_worker.py; only
// the OS primitives differ:
//   Windows CreateFileMappingW/MapViewOfFile -> POSIX shm_open + mmap
//   Windows CreateEventW/SetEvent/WaitForSingleObject -> polling a sequence
//     number already living in shared memory (see worker/rvc_worker.py's
//     PosixSequenceWaiter for the matching Python-side design note)
//   Windows CreateProcessW -> posix_spawn
#if !defined(__APPLE__) && !defined(__linux__)
#error WorkerClient_mac.cpp targets POSIX (macOS/Linux); use WorkerClient_win.cpp on Windows.
#endif

#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "IPlugPaths.h"

extern char** environ;

namespace rvc {
namespace {

constexpr uint32_t kMagic = 0x50564352; // RVCP
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kHeaderBytes = 4096;
constexpr uint32_t kMaxFrames = 131072;
constexpr uint32_t kMapBytes = kHeaderBytes + kMaxFrames * sizeof(float) * 2;
constexpr uint32_t kInputOffset = kHeaderBytes;
constexpr uint32_t kOutputOffset = kHeaderBytes + kMaxFrames * sizeof(float);
constexpr uint32_t kStatusTextOffset = 128;
constexpr uint32_t kStatusTextBytes = 512;

template <typename T>
void writeAt(void* const base, const std::size_t offset, const T value)
{
    std::memcpy(static_cast<unsigned char*>(base) + offset, &value, sizeof(T));
}

template <typename T>
T readAt(const void* const base, const std::size_t offset)
{
    T value {};
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(T));
    return value;
}

std::string jsonEscape(const std::string& text)
{
    std::ostringstream out;
    for (const unsigned char ch : text) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
            else
                out << static_cast<char>(ch);
        }
    }
    return out.str();
}

// POSIX shm_open names historically have a short platform limit (macOS: the
// legacy XNU shared-memory table caps names well under 32 bytes); keep this
// short and hex-only to stay safely inside that bound on every host.
std::string shortInstanceTag()
{
    static std::atomic<uint32_t> counter {0};
    const unsigned seed = static_cast<unsigned>(::getpid())
        ^ (counter.fetch_add(1) << 16)
        ^ static_cast<unsigned>(::time(nullptr));
    std::ostringstream out;
    out << "rvc" << std::hex << seed; // no leading slash: see call sites for why
    return out.str();
}

bool isFile(const std::string& path)
{
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool ensureDirectory(const std::string& path, std::string& error)
{
    if (::mkdir(path.c_str(), 0700) == 0)
        return true;
    if (errno == EEXIST) {
        struct stat info {};
        if (::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
            return true;
    }
    error = "Cannot create " + path + " (errno " + std::to_string(errno) + ")";
    return false;
}

std::string temporaryLogDirectory(std::string& error)
{
    const char* base = std::getenv("TMPDIR");
    std::string productDirectory = (base != nullptr && *base != '\0') ? base : "/tmp/";
    if (productDirectory.back() != '/')
        productDirectory += '/';
    productDirectory += "RVCRealtime";
    if (!ensureDirectory(productDirectory, error))
        return {};
    const std::string logDirectory = productDirectory + "/logs";
    if (!ensureDirectory(logDirectory, error))
        return {};
    return logDirectory;
}

// Resolves worker/rvc_worker.py, which CMakeLists.txt copies flat into the
// bundle's Contents/Resources/ (see the RESOURCES list for RVCRealtime-app /
// RVCRealtime-au). Falls back to a path relative to this binary for a
// non-bundled (e.g. rvc-worker-smoke) executable.
std::string workerScriptPath()
{
    // Dev/test override (rvc-worker-smoke isn't a bundle, so BundleResourcePath
    // below has nothing to resolve for it). Not used by the real AU/APP plugin.
    if (const char* override = std::getenv("RVC_WORKER_SCRIPT")) {
        if (isFile(override))
            return override;
    }
    WDL_String resourcePath;
#if defined AU_API
    iplug::BundleResourcePath(resourcePath, "org.RVCProject.audiounit.RVCRealtime");
#elif defined APP_API
    iplug::BundleResourcePath(resourcePath, "org.RVCProject.app.RVCRealtime");
#endif
    if (resourcePath.GetLength() > 0) {
        const std::string candidate = std::string(resourcePath.Get()) + "/rvc_worker.py";
        if (isFile(candidate))
            return candidate;
    }
    return {}; // caller reports kStatusError; there is no further platform fallback
}

} // namespace

struct WorkerClient::Ipc {
    int shmFd = -1;
    void* view = nullptr;
    std::string shmName;
    std::string configPath;
    pid_t pid = -1;
    bool spawned = false;
    uint32_t sequence = 0;

    ~Ipc()
    {
        if (view != nullptr && view != MAP_FAILED)
            ::munmap(view, kMapBytes);
        if (shmFd >= 0)
            ::close(shmFd);
        if (!shmName.empty())
            ::shm_unlink(("/" + shmName).c_str());
    }
};

WorkerClient::WorkerClient()
{
    parameters_[kParamPitch].store(12.0f);
    parameters_[kParamFormant].store(0.0f);
    parameters_[kParamIndexRate].store(0.0f);
    parameters_[kParamRmsMix].store(0.5f);
    parameters_[kParamThreshold].store(-60.0f);
    parameters_[kParamBlockMs].store(130.0f);
    parameters_[kParamCrossfadeMs].store(80.0f);
    parameters_[kParamExtraMs].store(2000.0f);
    parameters_[kParamF0Method].store(0.0f);

    paths_.model = RVC_DEFAULT_MODEL;
    paths_.index = RVC_DEFAULT_INDEX;
    paths_.rvcRoot = RVC_DEFAULT_ROOT;
    paths_.python = RVC_DEFAULT_PYTHON;

    thread_ = std::thread(&WorkerClient::threadMain, this);
}

WorkerClient::~WorkerClient()
{
    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
}

void WorkerClient::setEnabled(const bool enabled) noexcept
{
    if (enabled_.exchange(enabled, std::memory_order_acq_rel) != enabled)
        configVersion_.fetch_add(1, std::memory_order_release);
}

void WorkerClient::setSampleRate(const double sampleRate) noexcept
{
    if (std::abs(sampleRate_.load(std::memory_order_relaxed) - sampleRate) > 0.5) {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        configVersion_.fetch_add(1, std::memory_order_release);
    }
}

void WorkerClient::setParameter(const ParameterId id, const float value) noexcept
{
    if (id >= kParameterCount)
        return;
    const float old = parameters_[id].exchange(value, std::memory_order_relaxed);
    if ((id == kParamBlockMs || id == kParamCrossfadeMs || id == kParamExtraMs) && std::abs(old - value) > 0.01f)
        configVersion_.fetch_add(1, std::memory_order_release);
}

void WorkerClient::setPath(const StateId id, const char* const value)
{
    if (value == nullptr)
        return;
    std::lock_guard<std::mutex> lock(pathsMutex_);
    switch (id) {
    case kStateModelPath: paths_.model = value; break;
    case kStateIndexPath: paths_.index = value; break;
    case kStateRvcRoot: paths_.rvcRoot = value; break;
    case kStatePythonPath: paths_.python = value; break;
    default: return;
    }
    configVersion_.fetch_add(1, std::memory_order_release);
}

std::size_t WorkerClient::pushInput(const float* const samples, const std::size_t count) noexcept
{
    if (!isReady())
        return 0;
    const std::size_t pushed = inputRing_.push(samples, count);
    if (pushed != count)
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    return pushed;
}

std::size_t WorkerClient::popOutput(float* const samples, const std::size_t count) noexcept
{
    if (!isReady())
        return 0;
    return outputRing_.pop(samples, count);
}

std::string WorkerClient::statusText() const
{
    std::lock_guard<std::mutex> lock(statusTextMutex_);
    return statusText_;
}

WorkerClient::Paths WorkerClient::pathsSnapshot() const
{
    std::lock_guard<std::mutex> lock(pathsMutex_);
    return paths_;
}

void WorkerClient::setStatus(const int status, const std::string& text)
{
    status_.store(status, std::memory_order_release);
    std::lock_guard<std::mutex> lock(statusTextMutex_);
    statusText_ = text;
}

uint32_t WorkerClient::calculateBlockFrames() const noexcept
{
    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    const double zc = std::max(1.0, std::floor(sampleRate / 100.0));
    const double seconds = parameters_[kParamBlockMs].load(std::memory_order_relaxed) / 1000.0;
    const auto frames = static_cast<uint32_t>(std::round(seconds * sampleRate / zc) * zc);
    return std::clamp<uint32_t>(frames, static_cast<uint32_t>(zc), kMaxFrames);
}

std::string WorkerClient::writeWorkerConfig(const Paths& paths, std::string& error) const
{
    const pid_t pid = ::getpid();
    const std::string logDirectory = temporaryLogDirectory(error);
    if (logDirectory.empty())
        return {};

    static std::atomic<uint32_t> instanceCounter {0};
    std::ostringstream name;
    name << logDirectory << "/instance_" << pid << "_" << instanceCounter.fetch_add(1) << ".json";
    const std::string configPath = name.str();

    std::ostringstream json;
    json << "{\n"
         << "  \"rvc_root\": \"" << jsonEscape(paths.rvcRoot) << "\",\n"
         << "  \"model_path\": \"" << jsonEscape(paths.model) << "\",\n"
         << "  \"index_path\": \"" << jsonEscape(paths.index) << "\",\n"
         << "  \"sample_rate\": " << static_cast<uint32_t>(sampleRate_.load()) << ",\n"
         << "  \"block_ms\": " << parameters_[kParamBlockMs].load() << ",\n"
         << "  \"crossfade_ms\": " << parameters_[kParamCrossfadeMs].load() << ",\n"
         << "  \"extra_ms\": " << parameters_[kParamExtraMs].load() << "\n"
         << "}\n";
    const std::string contents = json.str();

    const int fd = ::open(configPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        error = "Cannot create " + configPath + " (errno " + std::to_string(errno) + ")";
        return {};
    }
    const ssize_t written = ::write(fd, contents.data(), contents.size());
    ::close(fd);
    if (written < 0 || static_cast<std::size_t>(written) != contents.size()) {
        ::unlink(configPath.c_str());
        error = "Cannot write " + configPath + " (errno " + std::to_string(errno) + ")";
        return {};
    }
    error.clear();
    return configPath;
}

bool WorkerClient::launchWorker(const Paths& paths, const uint64_t)
{
    ipc_ = std::make_unique<Ipc>();
    ipc_->shmName = shortInstanceTag();

    ipc_->shmFd = ::shm_open(("/" + ipc_->shmName).c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (ipc_->shmFd < 0) {
        setStatus(kStatusError, "IPC initialization failed (shm_open errno " + std::to_string(errno) + ")");
        stopWorker();
        return false;
    }
    if (::ftruncate(ipc_->shmFd, static_cast<off_t>(kMapBytes)) != 0) {
        setStatus(kStatusError, "IPC initialization failed (ftruncate errno " + std::to_string(errno) + ")");
        stopWorker();
        return false;
    }
    ipc_->view = ::mmap(nullptr, kMapBytes, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_->shmFd, 0);
    if (ipc_->view == MAP_FAILED) {
        ipc_->view = nullptr;
        setStatus(kStatusError, "Shared memory mapping failed (errno " + std::to_string(errno) + ")");
        stopWorker();
        return false;
    }
    std::memset(ipc_->view, 0, kMapBytes);
    writeAt<uint32_t>(ipc_->view, 0, kMagic);
    writeAt<uint32_t>(ipc_->view, 4, kProtocolVersion);
    writeAt<int32_t>(ipc_->view, 8, kStatusStarting);

    std::string configError;
    ipc_->configPath = writeWorkerConfig(paths, configError);
    if (ipc_->configPath.empty()) {
        setStatus(kStatusError, configError.empty() ? "Could not write worker configuration" : configError);
        stopWorker();
        return false;
    }

    const std::string workerScript = workerScriptPath();
    if (workerScript.empty()) {
        setStatus(kStatusError, "Plugin worker resource is missing");
        stopWorker();
        return false;
    }

    const std::string logPath = ipc_->configPath + ".process.log";
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(),
                                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    std::vector<char*> argv;
    std::vector<std::string> argStorage {
        paths.python, "-I", workerScript,
        "--map", ipc_->shmName,
        "--config", ipc_->configPath,
    };
    argv.reserve(argStorage.size() + 1);
    for (auto& arg : argStorage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawnResult = ::posix_spawn(&pid, paths.python.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawnResult != 0) {
        setStatus(kStatusError, "Python worker launch failed (posix_spawn errno " + std::to_string(spawnResult) + ")");
        stopWorker();
        return false;
    }
    ipc_->pid = pid;
    ipc_->spawned = true;
    setStatus(kStatusLoading, "Loading RVC model");
    return true;
}

void WorkerClient::stopWorker()
{
    ready_.store(false, std::memory_order_release);
    if (ipc_ && ipc_->view != nullptr) {
        writeAt<int32_t>(ipc_->view, 8, -2);
        // No named event to signal on POSIX: bump the sequence field so the
        // worker's PosixSequenceWaiter (polling offset 12) wakes and re-checks
        // status. See worker/rvc_worker.py.
        writeAt<uint32_t>(ipc_->view, 12, ++ipc_->sequence);
    }
    if (ipc_ && ipc_->spawned && ipc_->pid > 0) {
        int status = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
        pid_t waited = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            waited = ::waitpid(ipc_->pid, &status, WNOHANG);
            if (waited == ipc_->pid)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (waited != ipc_->pid) {
            ::kill(ipc_->pid, SIGTERM);
            const auto killDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < killDeadline) {
                if (::waitpid(ipc_->pid, &status, WNOHANG) == ipc_->pid) {
                    waited = ipc_->pid;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (waited != ipc_->pid) {
                ::kill(ipc_->pid, SIGKILL);
                ::waitpid(ipc_->pid, &status, 0);
            }
        }
    }
    ipc_.reset();
}

bool WorkerClient::processOneBlock()
{
    if (!ipc_ || !ipc_->view)
        return false;
    const uint32_t frames = blockFrames_.load(std::memory_order_relaxed);
    if (inputRing_.readable() < frames)
        return true;
    if (requestBuffer_.size() < frames) {
        requestBuffer_.resize(frames);
        responseBuffer_.resize(frames);
    }
    if (inputRing_.pop(requestBuffer_.data(), frames) != frames)
        return true;

    const uint32_t sequence = ++ipc_->sequence;
    writeAt<uint32_t>(ipc_->view, 20, frames);
    writeAt<uint32_t>(ipc_->view, 24, static_cast<uint32_t>(sampleRate_.load(std::memory_order_relaxed)));
    writeAt<float>(ipc_->view, 32, parameters_[kParamPitch].load(std::memory_order_relaxed));
    writeAt<float>(ipc_->view, 36, parameters_[kParamFormant].load(std::memory_order_relaxed));
    writeAt<float>(ipc_->view, 40, parameters_[kParamIndexRate].load(std::memory_order_relaxed));
    writeAt<float>(ipc_->view, 44, parameters_[kParamRmsMix].load(std::memory_order_relaxed));
    writeAt<float>(ipc_->view, 48, parameters_[kParamThreshold].load(std::memory_order_relaxed));
    writeAt<uint32_t>(ipc_->view, 64, static_cast<uint32_t>(std::round(parameters_[kParamF0Method].load(std::memory_order_relaxed))));
    std::memcpy(static_cast<unsigned char*>(ipc_->view) + kInputOffset, requestBuffer_.data(), frames * sizeof(float));
    // Writing the sequence last is the "signal": worker polls this field.
    writeAt<uint32_t>(ipc_->view, 12, sequence);

    const auto timeoutMs = std::max<int64_t>(5000, static_cast<int64_t>(parameters_[kParamBlockMs].load() * 8.0f));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool responded = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (readAt<uint32_t>(ipc_->view, 16) == sequence) {
            responded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!responded) {
        setStatus(kStatusError, "RVC inference timed out");
        return false;
    }

    const int workerState = readAt<int32_t>(ipc_->view, 8);
    if (workerState < 0) {
        const char* const text = reinterpret_cast<const char*>(static_cast<unsigned char*>(ipc_->view) + kStatusTextOffset);
        setStatus(kStatusError, text[0] != '\0' ? text : "Worker error");
        return false;
    }

    inferMs_.store(readAt<float>(ipc_->view, 56), std::memory_order_relaxed);
    std::memcpy(responseBuffer_.data(), static_cast<unsigned char*>(ipc_->view) + kOutputOffset, frames * sizeof(float));
    if (outputRing_.push(responseBuffer_.data(), frames) != frames)
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void WorkerClient::threadMain()
{
    uint64_t activeVersion = 0;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        const bool enabled = enabled_.load(std::memory_order_acquire);
        const uint64_t requestedVersion = configVersion_.load(std::memory_order_acquire);
        if (!enabled) {
            if (ipc_)
                stopWorker();
            setStatus(kStatusOff, "Off");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        if (!ipc_ || activeVersion != requestedVersion) {
            stopWorker();
            const Paths paths = pathsSnapshot();
            if (paths.model.empty() || paths.rvcRoot.empty() || paths.python.empty()) {
                setStatus(kStatusError, "Select a model and RVC runtime");
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            activeVersion = requestedVersion;
            blockFrames_.store(calculateBlockFrames(), std::memory_order_relaxed);
            latencyFrames_.store(blockFrames_.load() * 2, std::memory_order_relaxed);
            droppedBlocks_.store(0, std::memory_order_relaxed);
            inferMs_.store(0.0f, std::memory_order_relaxed);
            if (!launchWorker(paths, activeVersion)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        if (!ready_.load(std::memory_order_acquire)) {
            const int workerState = readAt<int32_t>(ipc_->view, 8);
            const char* const workerText = reinterpret_cast<const char*>(static_cast<unsigned char*>(ipc_->view) + kStatusTextOffset);
            if (workerState == kStatusReady) {
                inputRing_.resetUnsafe();
                outputRing_.resetUnsafe();
                outputRing_.pushZeros(latencyFrames_.load(std::memory_order_relaxed));
                setStatus(kStatusReady, workerText[0] != '\0' ? workerText : "Ready");
                ready_.store(true, std::memory_order_release);
            } else if (workerState < 0) {
                setStatus(kStatusError, workerText[0] != '\0' ? workerText : "Model load failed");
                stopWorker();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            } else if (workerText[0] != '\0') {
                setStatus(kStatusLoading, workerText);
            }
            int exitStatus = 0;
            const pid_t exited = ::waitpid(ipc_->pid, &exitStatus, WNOHANG);
            if (exited == ipc_->pid) {
                ipc_->spawned = false; // already reaped
                if (workerState >= 0)
                    setStatus(kStatusError, "Python worker exited with code " + std::to_string(WEXITSTATUS(exitStatus)));
                stopWorker();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (!processOneBlock()) {
            ready_.store(false, std::memory_order_release);
            stopWorker();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }
        if (inputRing_.readable() < blockFrames_.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stopWorker();
}

} // namespace rvc
