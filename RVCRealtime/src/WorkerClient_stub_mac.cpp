#include "WorkerClient.hpp"

// Placeholder used until issue #3 (macOS process/IPC abstraction) and issue #6
// (macOS Python worker/runtime integration) land. It compiles and lets the
// IGraphics UI render and be driven for a human GUI check (see
// docs/macos-runtime-bootstrap.ja.md), but never launches a worker process or
// converts audio: the engine reports "not implemented" and ProcessBlock() always
// takes RVCRealtime's passthrough path (mWorker.isReady() stays false).
// This is a stand-in for model-integration testing, not a functional backend —
// do not treat it as evidence that macOS voice conversion works end-to-end.

namespace rvc {

// WorkerClient::Ipc is only forward-declared in the header; std::unique_ptr<Ipc>'s
// destructor needs a complete type wherever ~WorkerClient() is instantiated.
struct WorkerClient::Ipc {};

WorkerClient::WorkerClient() = default;
WorkerClient::~WorkerClient() = default;

void WorkerClient::setEnabled(bool) noexcept {}
void WorkerClient::setSampleRate(double) noexcept {}
void WorkerClient::setParameter(ParameterId, float) noexcept {}
void WorkerClient::setPath(StateId, const char*) {}

std::size_t WorkerClient::pushInput(const float*, std::size_t) noexcept { return 0; }
std::size_t WorkerClient::popOutput(float*, std::size_t) noexcept { return 0; }

std::string WorkerClient::statusText() const
{
  return "macOS worker not implemented (GUI stub, see issue #3/#6)";
}

} // namespace rvc
