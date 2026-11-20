#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <variant>
#include <vector>

#include "simulator/core/cpu_state.h"
#include "simulator/env/buffered_console.h"
#include "simulator/loader/program_image.h"
#include "simulator/runtime/machine.h"

namespace rvsim {

// High-level execution state shown by the UI.
enum class SimState : uint8_t {
  kNoProgram,        // Nothing assembled/loaded (or assembly failed).
  kPaused,           // Program loaded and stopped; can step or run.
  kRunning,          // Executing on the worker thread.
  kWaitingForInput,  // Blocked in a console-input ecall.
  kExited,           // Program exited; reset to run again.
  kFaulted,          // Unhandled trap; reset to run again.
};

constexpr std::string_view ToString(SimState state) {
  switch (state) {
    case SimState::kNoProgram:
      return "No program";
    case SimState::kPaused:
      return "Paused";
    case SimState::kRunning:
      return "Running";
    case SimState::kWaitingForInput:
      return "Waiting for input";
    case SimState::kExited:
      return "Exited";
    case SimState::kFaulted:
      return "Faulted";
  }
  return "?";
}

// Why the simulation last stopped or changed state.
enum class StopReason : uint8_t {
  kNone,
  kAssembled,
  kAssemblyFailed,
  kStep,
  kPaused,
  kBreakpoint,
  kEbreak,
  kExited,
  kFaulted,
  kWaitingForInput,
  kReset,
  kConfigChanged,
};

constexpr std::string_view ToString(StopReason reason) {
  switch (reason) {
    case StopReason::kNone:
      return "";
    case StopReason::kAssembled:
      return "Assembled";
    case StopReason::kAssemblyFailed:
      return "Assembly failed";
    case StopReason::kStep:
      return "Step";
    case StopReason::kPaused:
      return "Paused";
    case StopReason::kBreakpoint:
      return "Breakpoint";
    case StopReason::kEbreak:
      return "ebreak";
    case StopReason::kExited:
      return "Exited";
    case StopReason::kFaulted:
      return "Fault";
    case StopReason::kWaitingForInput:
      return "Waiting for input";
    case StopReason::kReset:
      return "Reset";
    case StopReason::kConfigChanged:
      return "Configuration changed";
  }
  return "";
}

// A memory range the UI wants mirrored into every snapshot.
struct MemoryRequest {
  uint64_t address = 0;
  uint32_t size = 0;

  bool operator==(const MemoryRequest&) const = default;
};

struct MemorySlice {
  uint64_t address = 0;
  std::vector<uint8_t> bytes{};
  std::vector<uint8_t> mapped{};  // 1 if the byte's page is allocated.
};

// Immutable, frame-consistent copy of everything the UI renders.
struct UiSnapshot {
  uint64_t version = 0;
  // Incremented whenever a new program is assembled (or assembly fails).
  uint64_t program_generation = 0;
  SimState state = SimState::kNoProgram;
  StopReason stop_reason = StopReason::kNone;
  std::string message{};
  MachineConfig config{};
  CpuState cpu{};
  // CPU state before the most recent step / run interval, for highlighting.
  CpuState prev_cpu{};
  std::shared_ptr<const ProgramImage> program{};
  std::vector<Diagnostic> diagnostics{};
  std::string console{};
  bool console_truncated = false;
  std::vector<MemorySlice> memory{};  // Same order as the requests.
  uint64_t speed_hz = 0;              // 0 = unlimited.
  double effective_hz = 0.0;
  int64_t exit_code = 0;
};

// Owns a Machine on one dedicated worker thread. All public methods are
// thread-safe and non-blocking (except WaitForSnapshot); commands execute in
// order on the worker, which publishes a new UiSnapshot at most ~60 times per
// second while running and immediately on every other state change.
class SimulationController {
 public:
  static constexpr size_t kMaxConsoleBytes = 256 * 1024;
  static constexpr uint32_t kMaxViewportBytes = 64 * 1024;

  explicit SimulationController(const MachineConfig& config = {});
  ~SimulationController();

  SimulationController(const SimulationController&) = delete;
  SimulationController& operator=(const SimulationController&) = delete;

  void Assemble(std::string source, const MachineConfig& config);
  void Run();
  void Pause();
  void StepIn();
  void Reset();
  // Address breakpoints (e.g. set from the disassembly).
  void SetBreakpoints(std::vector<uint64_t> addresses);
  // Source-line breakpoints (1-based), remapped through the source map of
  // every newly assembled program before it can run.
  void SetSourceBreakpoints(std::vector<int> lines);
  void SetSpeedHz(uint64_t hz);
  void SendConsoleInput(std::string text);
  void ClearConsole();
  void WriteRegister(uint8_t index, uint64_t value);
  void WritePc(uint64_t value);
  void WriteMemory(uint64_t address, std::vector<uint8_t> bytes);
  void SetConfig(const MachineConfig& config);
  void SetMemoryViewports(std::vector<MemoryRequest> requests);

  // Latest published snapshot (never null).
  std::shared_ptr<const UiSnapshot> snapshot() const;

  // Blocks until a published snapshot satisfies `predicate` or the timeout
  // expires. Intended for tests and headless tools; never call it from the
  // browser main thread.
  bool WaitForSnapshot(const std::function<bool(const UiSnapshot&)>& predicate,
                       std::chrono::milliseconds timeout) const;

 private:
  struct AssembleCmd {
    std::string source;
    MachineConfig config;
  };
  struct RunCmd {};
  struct PauseCmd {};
  struct StepCmd {};
  struct ResetCmd {};
  struct BreakpointsCmd {
    std::vector<uint64_t> addresses;
  };
  struct SourceBreakpointsCmd {
    std::vector<int> lines;
  };
  struct SpeedCmd {
    uint64_t hz;
  };
  struct InputCmd {
    std::string text;
  };
  struct ClearConsoleCmd {};
  struct WriteRegCmd {
    uint8_t index;
    uint64_t value;
  };
  struct WritePcCmd {
    uint64_t value;
  };
  struct WriteMemCmd {
    uint64_t address;
    std::vector<uint8_t> bytes;
  };
  struct ConfigCmd {
    MachineConfig config;
  };
  struct ViewportCmd {
    std::vector<MemoryRequest> requests;
  };
  using Command = std::variant<AssembleCmd, RunCmd, PauseCmd, StepCmd, ResetCmd,
                               BreakpointsCmd, SourceBreakpointsCmd, SpeedCmd,
                               InputCmd, ClearConsoleCmd, WriteRegCmd,
                               WritePcCmd, WriteMemCmd, ConfigCmd, ViewportCmd>;
  using Clock = std::chrono::steady_clock;

  enum class ResumeMode : uint8_t { kRun, kStep };

  void Post(Command command);
  void WorkerLoop();
  void Process(Command& command);
  void DoAssemble(AssembleCmd& cmd);
  void DoStep();
  void StartRunning();
  void ExecuteSlice();
  void HandleRunResult(const RunResult& result);
  void RebuildBreakpoints();
  void Stop(SimState state, StopReason reason);
  bool CanEdit() const;
  void Publish();

  // ---- shared state (guarded by mutex_) ----
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  mutable std::condition_variable publish_cv_;
  std::vector<Command> queue_;
  bool stop_requested_ = false;
  std::shared_ptr<const UiSnapshot> snapshot_;

  // ---- worker-owned state ----
  BufferedConsole console_;
  Machine machine_;
  MachineConfig config_;
  std::shared_ptr<const ProgramImage> program_;
  std::vector<Diagnostic> diagnostics_;
  std::vector<uint64_t> address_breakpoints_;
  std::vector<int> line_breakpoints_;
  std::unordered_set<uint64_t> breakpoints_;  // Effective address set.
  std::vector<MemoryRequest> viewports_;
  SimState state_ = SimState::kNoProgram;
  StopReason stop_reason_ = StopReason::kNone;
  std::string message_;
  ResumeMode resume_mode_ = ResumeMode::kRun;
  CpuState prev_cpu_;
  uint64_t speed_hz_ = 0;
  uint64_t version_ = 0;
  uint64_t program_generation_ = 0;
  bool dirty_ = true;        // State changed; publish immediately.
  bool first_chunk_ = true;  // Next chunk resumes a run.
  Clock::time_point run_start_;
  uint64_t run_start_instret_ = 0;
  Clock::time_point last_publish_;
  uint64_t last_publish_instret_ = 0;
  double effective_hz_ = 0.0;

  std::thread worker_;  // Last member: started after everything else.
};

}  // namespace rvsim
