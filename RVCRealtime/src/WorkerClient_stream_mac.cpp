#include "WorkerClient.hpp"
#include "config.h"

#if !defined(__APPLE__)
#error WorkerClient_stream_mac.cpp is the macOS localhost RSVC adapter.
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace rvc {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kMagic = 0x43565352; // little-endian bytes: RSVC
constexpr uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderBytes = 32;
constexpr uint32_t kMaxPayloadBytes = 1u << 20;
constexpr uint32_t kMaxFrames = 131072;
constexpr uint16_t kStreamPort = 17865;
constexpr int kIoSliceMs = 50;
constexpr uint32_t kAudioFlagDiscontinuous = 1u << 0;
constexpr uint32_t kAudioFlagOffline = 1u << 1;

enum FrameType : uint16_t {
    kHello = 0x0001,
    kHelloAck = 0x0002,
    kHelloNak = 0x0003,
    kSessionOpen = 0x0010,
    kSessionAccept = 0x0011,
    kSessionReject = 0x0012,
    kConfigUpdate = 0x0020,
    kConfigAck = 0x0021,
    kAudioIn = 0x0030,
    kAudioOut = 0x0031,
    kAudioSkip = 0x0032,
    kState = 0x0040,
    kHeartbeat = 0x0041,
    kHeartbeatAck = 0x0042,
    kError = 0x0050,
    kClose = 0x0051,
};

struct WireFrame {
    uint16_t type = 0;
    uint32_t sessionId = 0;
    uint32_t sequence = 0;
    uint64_t timestampNs = 0;
    std::vector<unsigned char> payload;
};

void appendU16(std::vector<unsigned char>& out, const uint16_t value)
{
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

void appendU32(std::vector<unsigned char>& out, const uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<unsigned char>(value >> shift));
}

void appendU64(std::vector<unsigned char>& out, const uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<unsigned char>(value >> shift));
}

void appendFloat(std::vector<unsigned char>& out, const float value)
{
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "RSVC requires IEEE754 binary32");
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

uint16_t readU16(const unsigned char* const data)
{
    return static_cast<uint16_t>(data[0])
        | static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const unsigned char* const data)
{
    return static_cast<uint32_t>(data[0])
        | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16)
        | (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t readU64(const unsigned char* const data)
{
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<uint64_t>(data[shift / 8]) << shift;
    return value;
}

float readFloat(const unsigned char* const data)
{
    const uint32_t bits = readU32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void appendString(std::vector<unsigned char>& out, const std::string& value)
{
    const std::size_t size = std::min<std::size_t>(value.size(), 1024);
    appendU16(out, static_cast<uint16_t>(size));
    out.insert(out.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(size));
}

uint64_t monotonicNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
}

std::vector<unsigned char> packFrame(const uint16_t type,
                                     const std::vector<unsigned char>& payload = {},
                                     const uint32_t sessionId = 0,
                                     const uint32_t sequence = 0)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(kHeaderBytes + payload.size());
    appendU32(bytes, kMagic);
    appendU16(bytes, kProtocolVersion);
    appendU16(bytes, type);
    appendU32(bytes, sessionId);
    appendU32(bytes, sequence);
    appendU32(bytes, static_cast<uint32_t>(payload.size()));
    appendU32(bytes, 0); // crc32 is reserved and must be zero in v1
    appendU64(bytes, monotonicNs());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

uint32_t framesForMs(const double sampleRate, const float milliseconds)
{
    const double zc = std::max(1.0, std::floor(sampleRate / 100.0));
    const auto frames = static_cast<uint32_t>(std::round(milliseconds / 1000.0 * sampleRate / zc) * zc);
    return std::clamp<uint32_t>(frames, static_cast<uint32_t>(zc), kMaxFrames);
}

} // namespace

struct WorkerClient::Ipc {
    int fd = -1;
    uint32_t sessionId = 0;
    uint32_t audioSequence = 0;
    uint32_t heartbeatSequence = 0;
    uint32_t configSequence = 0;
    uint32_t maxInFlight = 1;
    Clock::time_point nextHeartbeat {};
    Clock::time_point lastHeartbeatAck {};
    std::vector<unsigned char> received;

    ~Ipc()
    {
        if (fd >= 0)
            ::close(fd);
    }
};

namespace {

bool waitForFd(const int fd, const short events, const int timeoutMs)
{
    pollfd descriptor {fd, events, 0};
    int result = 0;
    do {
        result = ::poll(&descriptor, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

bool sendAll(const int fd, const std::vector<unsigned char>& bytes,
             const std::atomic<bool>& stopRequested, const int timeoutMs)
{
    std::size_t offset = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (offset < bytes.size() && !stopRequested.load(std::memory_order_acquire)) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0)
            return false;
        if (!waitForFd(fd, POLLOUT, static_cast<int>(std::min<int64_t>(kIoSliceMs, remaining))))
            continue;
        const ssize_t sent = ::send(fd, bytes.data() + offset, bytes.size() - offset, 0);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
    }
    return offset == bytes.size();
}

template <typename IpcType>
bool receiveAvailable(IpcType& ipc, const int timeoutMs)
{
    if (!waitForFd(ipc.fd, POLLIN, timeoutMs))
        return true;
    unsigned char buffer[65536];
    for (;;) {
        const ssize_t count = ::recv(ipc.fd, buffer, sizeof(buffer), 0);
        if (count > 0) {
            ipc.received.insert(ipc.received.end(), buffer, buffer + count);
            if (ipc.received.size() > kHeaderBytes + kMaxPayloadBytes)
                return false;
            continue;
        }
        if (count == 0)
            return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        if (errno != EINTR)
            return false;
    }
}

template <typename IpcType>
int takeFrame(IpcType& ipc, WireFrame& frame)
{
    if (ipc.received.size() < kHeaderBytes)
        return 0;
    const unsigned char* header = ipc.received.data();
    const uint32_t magic = readU32(header);
    const uint16_t version = readU16(header + 4);
    const uint32_t payloadBytes = readU32(header + 16);
    const uint32_t crc = readU32(header + 20);
    if (magic != kMagic || version != kProtocolVersion || crc != 0 || payloadBytes > kMaxPayloadBytes)
        return -1;
    const std::size_t total = kHeaderBytes + payloadBytes;
    if (ipc.received.size() < total)
        return 0;
    frame.type = readU16(header + 6);
    frame.sessionId = readU32(header + 8);
    frame.sequence = readU32(header + 12);
    frame.timestampNs = readU64(header + 24);
    frame.payload.assign(ipc.received.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
                         ipc.received.begin() + static_cast<std::ptrdiff_t>(total));
    ipc.received.erase(ipc.received.begin(), ipc.received.begin() + static_cast<std::ptrdiff_t>(total));
    return 1;
}

template <typename IpcType>
bool receiveFrame(IpcType& ipc, WireFrame& frame,
                  const std::atomic<bool>& stopRequested, const int timeoutMs)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!stopRequested.load(std::memory_order_acquire)) {
        const int parsed = takeFrame(ipc, frame);
        if (parsed != 0)
            return parsed > 0;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0)
            return false;
        if (!receiveAvailable(ipc, static_cast<int>(std::min<int64_t>(kIoSliceMs, remaining))))
            return false;
    }
    return false;
}

int connectLoopback(const std::atomic<bool>& stopRequested)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int enabled = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(fd);
        return -1;
    }
    sockaddr_in address {};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(kStreamPort);
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
        return fd;
    if (errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    while (!stopRequested.load(std::memory_order_acquire) && Clock::now() < deadline) {
        if (!waitForFd(fd, POLLOUT, kIoSliceMs))
            continue;
        int socketError = 0;
        socklen_t size = sizeof(socketError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &size) == 0 && socketError == 0)
            return fd;
        break;
    }
    ::close(fd);
    return -1;
}

} // namespace

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
    if (paths_.model.empty())
        paths_.model = "active";
    thread_ = std::thread(&WorkerClient::threadMain, this);
}

WorkerClient::~WorkerClient()
{
    stopRequested_.store(true, std::memory_order_release);
    offlineCv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void WorkerClient::setEnabled(const bool enabled) noexcept
{
    if (enabled_.exchange(enabled, std::memory_order_acq_rel) != enabled) {
        configVersion_.fetch_add(1, std::memory_order_release);
        if (!enabled)
            offlineCv_.notify_all();
    }
}

void WorkerClient::setRenderingOffline(const bool offline) noexcept
{
    if (renderingOffline_.exchange(offline, std::memory_order_acq_rel) == offline)
        return;
    if (offline)
        offlineRenderCount_.fetch_add(1, std::memory_order_relaxed);
    // The management thread owns ring reset. Marking ready=false first prevents
    // a callback from joining the old generation while that reset is pending.
    ready_.store(false, std::memory_order_seq_cst);
    renderModeVersion_.fetch_add(1, std::memory_order_release);
}

bool WorkerClient::processOffline(const float* const input, const std::size_t inputCount,
                                  float* const output, const std::size_t outputCount,
                                  const uint32_t timeoutMs) noexcept
{
    if (input == nullptr || output == nullptr || inputCount == 0 || outputCount == 0)
        return false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    const uint64_t requestedMode = renderModeVersion_.load(std::memory_order_acquire);
    std::unique_lock<std::mutex> waitLock(offlineMutex_);
    if (!offlineCv_.wait_until(waitLock, deadline, [this, requestedMode]() {
            return stopRequested_.load(std::memory_order_acquire)
                || !enabled_.load(std::memory_order_acquire)
                || (ready_.load(std::memory_order_seq_cst)
                    && appliedRenderModeVersion_.load(std::memory_order_acquire) == requestedMode);
        })) {
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!ready_.load(std::memory_order_seq_cst) || !renderingOffline_.load(std::memory_order_acquire))
        return false;

    audioRingUsers_.fetch_add(1, std::memory_order_seq_cst);
    const auto leaveRingGeneration = [this]() {
        audioRingUsers_.fetch_sub(1, std::memory_order_seq_cst);
        offlineCv_.notify_all();
    };
    if (!ready_.load(std::memory_order_seq_cst)
        || appliedRenderModeVersion_.load(std::memory_order_acquire) != requestedMode) {
        leaveRingGeneration();
        return false;
    }

    if (!offlineCv_.wait_until(waitLock, deadline, [this, inputCount]() {
            return !ready_.load(std::memory_order_seq_cst) || inputRing_.writable() >= inputCount;
        }) || !ready_.load(std::memory_order_seq_cst)
        || inputRing_.push(input, inputCount) != inputCount) {
        leaveRingGeneration();
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!offlineCv_.wait_until(waitLock, deadline, [this, outputCount]() {
            return !ready_.load(std::memory_order_seq_cst) || outputRing_.readable() >= outputCount;
        }) || !ready_.load(std::memory_order_seq_cst)
        || outputRing_.pop(output, outputCount) != outputCount) {
        leaveRingGeneration();
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    leaveRingGeneration();
    return true;
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
    if (std::abs(old - value) <= 0.0001f)
        return;
    if (id == kParamBlockMs || id == kParamCrossfadeMs || id == kParamExtraMs) {
        configVersion_.fetch_add(1, std::memory_order_release);
    } else if (id == kParamPitch || id == kParamFormant || id == kParamIndexRate
               || id == kParamRmsMix || id == kParamThreshold || id == kParamF0Method) {
        parameterVersion_.fetch_add(1, std::memory_order_release);
    }
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
    if (!ready_.load(std::memory_order_seq_cst))
        return 0;
    audioRingUsers_.fetch_add(1, std::memory_order_seq_cst);
    if (!ready_.load(std::memory_order_seq_cst)) {
        audioRingUsers_.fetch_sub(1, std::memory_order_seq_cst);
        return 0;
    }
    const std::size_t pushed = inputRing_.push(samples, count);
    if (pushed != count)
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    audioRingUsers_.fetch_sub(1, std::memory_order_seq_cst);
    return pushed;
}

std::size_t WorkerClient::popOutput(float* const samples, const std::size_t count) noexcept
{
    if (!ready_.load(std::memory_order_seq_cst))
        return 0;
    audioRingUsers_.fetch_add(1, std::memory_order_seq_cst);
    if (!ready_.load(std::memory_order_seq_cst)) {
        audioRingUsers_.fetch_sub(1, std::memory_order_seq_cst);
        return 0;
    }
    const std::size_t popped = outputRing_.pop(samples, count);
    audioRingUsers_.fetch_sub(1, std::memory_order_seq_cst);
    return popped;
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

void WorkerClient::waitForAudioRingUsers() noexcept
{
    // ready_ is false before this point. A callback that observed the prior
    // generation either joined the count before this load or rechecks ready_
    // after joining and leaves without touching either ring.
    while (audioRingUsers_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();
}

uint32_t WorkerClient::calculateBlockFrames() const noexcept
{
    return framesForMs(sampleRate_.load(std::memory_order_relaxed),
                       parameters_[kParamBlockMs].load(std::memory_order_relaxed));
}

bool WorkerClient::launchWorker(const Paths& paths, const uint64_t)
{
    ipc_ = std::make_unique<Ipc>();
    ipc_->fd = connectLoopback(stopRequested_);
    if (ipc_->fd < 0) {
        setStatus(kStatusError, "RSVC runtime unavailable at 127.0.0.1:17865");
        ipc_.reset();
        return false;
    }

    std::vector<unsigned char> hello;
    appendU16(hello, 1);
    appendU16(hello, 1);
    appendU32(hello, 0);
    hello.push_back(2); // auv2
    hello.push_back(0);
    appendString(hello, "RVCRealtime");
    appendString(hello, "0.1.0");
    if (!sendAll(ipc_->fd, packFrame(kHello, hello), stopRequested_, 1000)) {
        stopWorker();
        return false;
    }
    WireFrame reply;
    if (!receiveFrame(*ipc_, reply, stopRequested_, 1000) || reply.type != kHelloAck
        || reply.sessionId != 0 || reply.sequence != 0) {
        setStatus(kStatusError, reply.type == kHelloNak ? "RSVC protocol rejected" : "RSVC HELLO failed");
        stopWorker();
        return false;
    }

    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    const uint32_t block = calculateBlockFrames();
    const uint32_t crossfade = framesForMs(sampleRate, parameters_[kParamCrossfadeMs].load(std::memory_order_relaxed));
    const uint32_t extra = framesForMs(sampleRate, parameters_[kParamExtraMs].load(std::memory_order_relaxed));
    std::vector<unsigned char> open;
    appendU32(open, 1); // request id
    appendU32(open, static_cast<uint32_t>(sampleRate));
    appendU16(open, 1);
    appendU16(open, 1); // f32le
    appendU32(open, block);
    appendU32(open, crossfade);
    appendU32(open, extra);
    appendU32(open, 1); // request prewarm
    appendString(open, paths.model.empty() ? "active" : paths.model);
    appendString(open, paths.index);
    appendString(open, "");
    setStatus(kStatusLoading, "Opening RSVC session");
    if (!sendAll(ipc_->fd, packFrame(kSessionOpen, open), stopRequested_, 1000)
        || !receiveFrame(*ipc_, reply, stopRequested_, 180000)) {
        setStatus(kStatusError, "RSVC session open timed out");
        stopWorker();
        return false;
    }
    if (reply.type != kSessionAccept || reply.payload.size() < 40) {
        setStatus(kStatusError, reply.type == kSessionReject ? "RSVC session rejected" : "Invalid SESSION_ACCEPT");
        stopWorker();
        return false;
    }
    const uint32_t payloadSession = readU32(reply.payload.data() + 4);
    const uint32_t acceptedRate = readU32(reply.payload.data() + 8);
    const uint32_t acceptedBlock = readU32(reply.payload.data() + 16);
    const uint32_t latency = readU32(reply.payload.data() + 28);
    if (payloadSession == 0 || reply.sessionId != payloadSession
        || acceptedRate != static_cast<uint32_t>(sampleRate) || acceptedBlock != block
        || latency >= kRingCapacity) {
        setStatus(kStatusError, "RSVC session parameters do not match");
        stopWorker();
        return false;
    }
    ipc_->sessionId = payloadSession;
    ipc_->maxInFlight = std::max<uint32_t>(1, readU32(reply.payload.data() + 32));
    blockFrames_.store(acceptedBlock, std::memory_order_relaxed);
    latencyFrames_.store(latency, std::memory_order_relaxed);
    ipc_->nextHeartbeat = Clock::now() + std::chrono::seconds(1);
    ipc_->lastHeartbeatAck = Clock::now();

    if (!applyParameters(parameterVersion_.load(std::memory_order_acquire))) {
        setStatus(kStatusError, "RSVC CONFIG_UPDATE rejected");
        stopWorker();
        return false;
    }

    // Only the management thread waits. The audio callback never spins or locks.
    waitForAudioRingUsers();
    inputRing_.resetUnsafe();
    outputRing_.resetUnsafe();
    outputRing_.pushZeros(latency);
    droppedBlocks_.store(0, std::memory_order_relaxed);
    inferMs_.store(0.0f, std::memory_order_relaxed);
    setStatus(kStatusReady, "RSVC 127.0.0.1:17865 session " + std::to_string(ipc_->sessionId));
    appliedRenderModeVersion_.store(renderModeVersion_.load(std::memory_order_acquire),
                                    std::memory_order_release);
    ready_.store(true, std::memory_order_seq_cst);
    offlineCv_.notify_all();
    return true;
}

bool WorkerClient::applyParameters(const uint64_t)
{
    if (!ipc_ || ipc_->sessionId == 0)
        return false;
    std::vector<unsigned char> config;
    config.reserve(24);
    appendFloat(config, parameters_[kParamPitch].load(std::memory_order_relaxed));
    appendFloat(config, parameters_[kParamFormant].load(std::memory_order_relaxed));
    appendFloat(config, parameters_[kParamIndexRate].load(std::memory_order_relaxed));
    appendFloat(config, parameters_[kParamRmsMix].load(std::memory_order_relaxed));
    appendFloat(config, parameters_[kParamThreshold].load(std::memory_order_relaxed));
    const float rawF0 = parameters_[kParamF0Method].load(std::memory_order_relaxed);
    appendU32(config, static_cast<uint32_t>(std::clamp(std::lround(rawF0), 0l, 2l)));
    const uint32_t sequence = ++ipc_->configSequence;
    if (!sendAll(ipc_->fd, packFrame(kConfigUpdate, config, ipc_->sessionId, sequence),
                 stopRequested_, 1000))
        return false;

    const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    while (Clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now()).count();
        WireFrame reply;
        if (!receiveFrame(*ipc_, reply, stopRequested_, static_cast<int>(remaining)))
            return false;
        if (reply.sessionId != ipc_->sessionId)
            return false;
        if (reply.type == kHeartbeatAck) {
            ipc_->lastHeartbeatAck = Clock::now();
            continue;
        }
        if (reply.type == kConfigAck && reply.sequence == sequence
            && reply.payload.size() == 4)
            return readU32(reply.payload.data()) == 0;
        if (reply.type == kError || reply.type == kClose)
            return false;
        return false;
    }
    return false;
}

void WorkerClient::stopWorker()
{
    ready_.store(false, std::memory_order_seq_cst);
    offlineCv_.notify_all();
    if (ipc_ && ipc_->fd >= 0) {
        std::vector<unsigned char> close;
        appendU32(close, 1); // engine_off / local reconnect
        appendU16(close, 0);
        (void) sendAll(ipc_->fd, packFrame(kClose, close, ipc_->sessionId), stopRequested_, kIoSliceMs);
        ::shutdown(ipc_->fd, SHUT_RDWR);
    }
    ipc_.reset();
}

bool WorkerClient::processOneBlock()
{
    if (!ipc_)
        return false;
    const auto sendHeartbeatIfDue = [this]() {
        if (Clock::now() < ipc_->nextHeartbeat)
            return true;
        std::vector<unsigned char> heartbeat;
        appendU64(heartbeat, monotonicNs());
        appendU32(heartbeat, 0);
        if (!sendAll(ipc_->fd, packFrame(kHeartbeat, heartbeat, ipc_->sessionId,
                                         ++ipc_->heartbeatSequence), stopRequested_, kIoSliceMs))
            return false;
        ipc_->nextHeartbeat = Clock::now() + std::chrono::seconds(1);
        return true;
    };
    const uint32_t frames = blockFrames_.load(std::memory_order_relaxed);
    if (inputRing_.readable() < frames) {
        if (!sendHeartbeatIfDue() || !receiveAvailable(*ipc_, 0))
            return false;
        WireFrame idleFrame;
        for (;;) {
            const int parsed = takeFrame(*ipc_, idleFrame);
            if (parsed < 0)
                return false;
            if (parsed == 0)
                break;
            if (idleFrame.sessionId != ipc_->sessionId)
                return false;
            if (idleFrame.type == kHeartbeatAck) {
                ipc_->lastHeartbeatAck = Clock::now();
            } else if (idleFrame.type == kState && idleFrame.payload.size() >= 16) {
                inferMs_.store(readFloat(idleFrame.payload.data() + 12), std::memory_order_relaxed);
                if (readU32(idleFrame.payload.data()) == static_cast<uint32_t>(kStatusError))
                    return false;
            } else if (idleFrame.type == kError || idleFrame.type == kClose) {
                return false;
            }
        }
        if (Clock::now() - ipc_->lastHeartbeatAck > std::chrono::seconds(3)
            && ipc_->heartbeatSequence != 0)
            return false;
        return true;
    }
    if (requestBuffer_.size() < frames) {
        requestBuffer_.resize(frames);
        responseBuffer_.resize(frames);
    }
    if (inputRing_.pop(requestBuffer_.data(), frames) != frames)
        return true;
    offlineCv_.notify_all();

    std::vector<unsigned char> audio;
    audio.reserve(24 + frames * sizeof(float));
    appendU32(audio, static_cast<uint32_t>(sampleRate_.load(std::memory_order_relaxed)));
    appendU16(audio, 1);
    appendU16(audio, 1);
    appendU32(audio, frames);
    appendU64(audio, monotonicNs());
    uint32_t audioFlags = 0;
    if (discontinuous_.exchange(false, std::memory_order_acq_rel))
        audioFlags |= kAudioFlagDiscontinuous;
    if (renderingOffline_.load(std::memory_order_acquire))
        audioFlags |= kAudioFlagOffline;
    appendU32(audio, audioFlags);
    for (uint32_t index = 0; index < frames; ++index)
        appendFloat(audio, requestBuffer_[index]);
    const uint32_t sequence = ++ipc_->audioSequence;
    const auto started = Clock::now();
    if (!sendAll(ipc_->fd, packFrame(kAudioIn, audio, ipc_->sessionId, sequence), stopRequested_, 3000))
        return false;

    const int timeoutMs = (audioFlags & kAudioFlagOffline) != 0
        ? 30000
        : std::max(5000, static_cast<int>(parameters_[kParamBlockMs].load() * 8.0f));
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!stopRequested_.load(std::memory_order_acquire) && enabled_.load(std::memory_order_acquire)
           && Clock::now() < deadline) {
        if (!sendHeartbeatIfDue())
            return false;
        if (!receiveAvailable(*ipc_, kIoSliceMs))
            return false;
        WireFrame frame;
        for (;;) {
            const int parsed = takeFrame(*ipc_, frame);
            if (parsed < 0)
                return false;
            if (parsed == 0)
                break;
            if (frame.sessionId != ipc_->sessionId)
                return false;
            if (frame.type == kHeartbeatAck) {
                ipc_->lastHeartbeatAck = Clock::now();
                continue;
            }
            if (frame.type == kState) {
                if (frame.payload.size() >= 16) {
                    inferMs_.store(readFloat(frame.payload.data() + 12), std::memory_order_relaxed);
                    if (readU32(frame.payload.data()) == static_cast<uint32_t>(kStatusError))
                        return false;
                }
                continue;
            }
            if (frame.type == kAudioSkip && frame.sequence == sequence) {
                if ((audioFlags & kAudioFlagOffline) != 0) {
                    setStatus(kStatusError, "RSVC skipped an offline render block");
                    return false;
                }
                outputRing_.pushZeros(frames);
                offlineCv_.notify_all();
                droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (frame.type == kAudioOut && frame.sequence == sequence) {
                if (frame.payload.size() != 24 + frames * sizeof(float)
                    || readU32(frame.payload.data()) != static_cast<uint32_t>(sampleRate_.load())
                    || readU16(frame.payload.data() + 4) != 1
                    || readU16(frame.payload.data() + 6) != 1
                    || readU32(frame.payload.data() + 8) != frames
                    || readU32(frame.payload.data() + 20) != audioFlags)
                    return false;
                for (uint32_t index = 0; index < frames; ++index)
                    responseBuffer_[index] = readFloat(frame.payload.data() + 24 + index * sizeof(float));
                inferMs_.store(static_cast<float>(std::chrono::duration<double, std::milli>(Clock::now() - started).count()),
                               std::memory_order_relaxed);
                if (outputRing_.push(responseBuffer_.data(), frames) != frames)
                    droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
                offlineCv_.notify_all();
                return true;
            }
            if (frame.type == kError || frame.type == kClose)
                return false;
        }
        if (Clock::now() - ipc_->lastHeartbeatAck > std::chrono::seconds(3)
            && ipc_->heartbeatSequence != 0)
            return false;
    }
    return false;
}

void WorkerClient::threadMain()
{
    uint64_t activeVersion = 0;
    uint64_t activeParameterVersion = 0;
    int backoffMs = 200;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (!enabled_.load(std::memory_order_acquire)) {
            if (ipc_)
                stopWorker();
            setStatus(kStatusOff, "Off");
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            backoffMs = 200;
            continue;
        }
        const uint64_t requestedVersion = configVersion_.load(std::memory_order_acquire);
        if (!ipc_ || requestedVersion != activeVersion) {
            stopWorker();
            activeVersion = requestedVersion;
            blockFrames_.store(calculateBlockFrames(), std::memory_order_relaxed);
            setStatus(kStatusStarting, "Connecting to RSVC 127.0.0.1:17865");
            if (!launchWorker(pathsSnapshot(), activeVersion)) {
                const auto deadline = Clock::now() + std::chrono::milliseconds(backoffMs);
                while (!stopRequested_.load(std::memory_order_acquire)
                       && enabled_.load(std::memory_order_acquire) && Clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                backoffMs = std::min(backoffMs * 2, 5000);
                continue;
            }
            activeParameterVersion = parameterVersion_.load(std::memory_order_acquire);
            backoffMs = 200;
        }
        const uint64_t requestedParameterVersion = parameterVersion_.load(std::memory_order_acquire);
        if (requestedParameterVersion != activeParameterVersion) {
            if (!applyParameters(requestedParameterVersion)) {
                setStatus(kStatusError, "RSVC parameter update failed; reconnecting");
                stopWorker();
                continue;
            }
            activeParameterVersion = requestedParameterVersion;
        }
        if (!processOneBlock()) {
            setStatus(kStatusError, "RSVC connection lost; reconnecting");
            stopWorker();
            continue;
        }
        const uint64_t requestedMode = renderModeVersion_.load(std::memory_order_acquire);
        if (appliedRenderModeVersion_.load(std::memory_order_acquire) != requestedMode) {
            ready_.store(false, std::memory_order_seq_cst);
            offlineCv_.notify_all();
            waitForAudioRingUsers();
            inputRing_.resetUnsafe();
            outputRing_.resetUnsafe();
            outputRing_.pushZeros(latencyFrames_.load(std::memory_order_relaxed));
            discontinuous_.store(true, std::memory_order_release);
            appliedRenderModeVersion_.store(requestedMode, std::memory_order_release);
            ready_.store(true, std::memory_order_seq_cst);
            offlineCv_.notify_all();
        }
        if (inputRing_.readable() < blockFrames_.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stopWorker();
}

} // namespace rvc
