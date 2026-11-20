#include "simulator/runtime/controller.h"

#include <algorithm>
#include <format>

#include "simulator/loader/assembler.h"

namespace rvsim {
namespace {

using namespace std::chrono_literals;

constexpr auto kPublishInterval = 16ms;  // ~60 Hz while running.
constexpr auto kSliceBudget = 8ms;       // Max uninterrupted execution.
constexpr uint64_t kChunkSteps = 4096;

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

}  // namespace

SimulationController::SimulationController(const MachineConfig& config)
    : snapshot_(std::make_shared<UiSnapshot>()),
      machine_(config, console_),
      config_(config) {
  Publish();
  worker_ = std::thread([this] { WorkerLoop(); });
}

SimulationController::~SimulationController() {
  {
    std::lock_guard lock(mutex_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void SimulationController::Post(Command command) {
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(command));
  }
  cv_.notify_one();
}

void SimulationController::Assemble(std::string source,
                                    const MachineConfig& config) {
  Post(AssembleCmd{std::move(source), config});
}
void SimulationController::Run() { Post(RunCmd{}); }
void SimulationController::Pause() { Post(PauseCmd{}); }
void SimulationController::StepIn() { Post(StepCmd{}); }
void SimulationController::Reset() { Post(ResetCmd{}); }
void SimulationController::SetBreakpoints(std::vector<uint64_t> addresses) {
  Post(BreakpointsCmd{std::move(addresses)});
}
void SimulationController::SetSourceBreakpoints(std::vector<int> lines) {
  Post(SourceBreakpointsCmd{std::move(lines)});
}
void SimulationController::SetSpeedHz(uint64_t hz) { Post(SpeedCmd{hz}); }
void SimulationController::SendConsoleInput(std::string text) {
  Post(InputCmd{std::move(text)});
}
void SimulationController::ClearConsole() { Post(ClearConsoleCmd{}); }
void SimulationController::WriteRegister(uint8_t index, uint64_t value) {
  Post(WriteRegCmd{index, value});
}
void SimulationController::WritePc(uint64_t value) { Post(WritePcCmd{value}); }
void SimulationController::WriteMemory(uint64_t address,
                                       std::vector<uint8_t> bytes) {
  Post(WriteMemCmd{address, std::move(bytes)});
}
void SimulationController::SetConfig(const MachineConfig& config) {
  Post(ConfigCmd{config});
}
void SimulationController::SetMemoryViewports(
    std::vector<MemoryRequest> requests) {
  Post(ViewportCmd{std::move(requests)});
}

std::shared_ptr<const UiSnapshot> SimulationController::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

bool SimulationController::WaitForSnapshot(
    const std::function<bool(const UiSnapshot&)>& predicate,
    std::chrono::milliseconds timeout) const {
  std::unique_lock lock(mutex_);
  return publish_cv_.wait_for(lock, timeout,
                              [&] { return predicate(*snapshot_); });
}

// ---------------------------------------------------------------- worker

void SimulationController::WorkerLoop() {
  std::vector<Command> batch;
  while (true) {
    {
      std::unique_lock lock(mutex_);
      const auto has_work = [&] { return stop_requested_ || !queue_.empty(); };
      if (state_ != SimState::kRunning) {
        cv_.wait(lock, has_work);
      } else if (speed_hz_ != 0) {
        // Sleep until the next instruction is due (or a command arrives).
        const uint64_t executed = machine_.state().instret - run_start_instret_;
        const auto due =
            run_start_ + std::chrono::duration_cast<Clock::duration>(
                             std::chrono::duration<double>(
                                 static_cast<double>(executed + 1) /
                                 static_cast<double>(speed_hz_)));
        const auto next_publish = last_publish_ + kPublishInterval;
        cv_.wait_until(lock, std::min(due, next_publish), has_work);
      }
      if (stop_requested_) {
        return;
      }
      batch.swap(queue_);
    }

    for (Command& command : batch) {
      Process(command);
    }
    batch.clear();

    if (state_ == SimState::kRunning) {
      ExecuteSlice();
    }

    const auto now = Clock::now();
    const bool progressed = machine_.state().instret != last_publish_instret_;
    if (dirty_ || (state_ == SimState::kRunning && progressed &&
                   now - last_publish_ >= kPublishInterval)) {
      Publish();
    }
  }
}

void SimulationController::Process(Command& command) {
  std::visit(Overloaded{
                 [&](AssembleCmd& cmd) { DoAssemble(cmd); },
                 [&](RunCmd&) {
                   if (state_ == SimState::kPaused) StartRunning();
                 },
                 [&](PauseCmd&) {
                   if (state_ == SimState::kRunning ||
                       state_ == SimState::kWaitingForInput) {
                     Stop(SimState::kPaused, StopReason::kPaused);
                   }
                 },
                 [&](StepCmd&) {
                   if (state_ == SimState::kPaused) DoStep();
                 },
                 [&](ResetCmd&) {
                   console_.Reset();
                   if (program_) {
                     machine_.Reset();
                     prev_cpu_ = machine_.state();
                     Stop(SimState::kPaused, StopReason::kReset);
                   } else {
                     dirty_ = true;
                   }
                 },
                 [&](BreakpointsCmd& cmd) {
                   address_breakpoints_ = std::move(cmd.addresses);
                   RebuildBreakpoints();
                 },
                 [&](SourceBreakpointsCmd& cmd) {
                   line_breakpoints_ = std::move(cmd.lines);
                   RebuildBreakpoints();
                 },
                 [&](SpeedCmd& cmd) {
                   speed_hz_ = cmd.hz;
                   run_start_ = Clock::now();
                   run_start_instret_ = machine_.state().instret;
                   dirty_ = true;
                 },
                 [&](InputCmd& cmd) {
                   console_.PushInput(cmd.text);
                   console_.AppendTranscript(cmd.text);
                   dirty_ = true;
                   if (state_ == SimState::kWaitingForInput) {
                     state_ = SimState::kPaused;
                     if (resume_mode_ == ResumeMode::kRun) {
                       StartRunning();
                     } else {
                       DoStep();
                     }
                   }
                 },
                 [&](ClearConsoleCmd&) {
                   console_.ClearOutput();
                   dirty_ = true;
                 },
                 [&](WriteRegCmd& cmd) {
                   if (CanEdit()) {
                     machine_.state().WriteReg(cmd.index, cmd.value);
                     dirty_ = true;
                   }
                 },
                 [&](WritePcCmd& cmd) {
                   if (CanEdit()) {
                     machine_.state().SetPc(cmd.value);
                     dirty_ = true;
                   }
                 },
                 [&](WriteMemCmd& cmd) {
                   if (CanEdit()) {
                     for (size_t i = 0; i < cmd.bytes.size(); ++i) {
                       (void)machine_.bus().Write8(cmd.address + i,
                                                   cmd.bytes[i]);
                     }
                     dirty_ = true;
                   }
                 },
                 [&](ConfigCmd& cmd) {
                   config_ = cmd.config;
                   machine_.Configure(config_);
                   program_.reset();
                   diagnostics_.clear();
                   console_.Reset();
                   ++program_generation_;
                   prev_cpu_ = machine_.state();
                   Stop(SimState::kNoProgram, StopReason::kConfigChanged);
                 },
                 [&](ViewportCmd& cmd) {
                   viewports_ = std::move(cmd.requests);
                   for (MemoryRequest& request : viewports_) {
                     request.size = std::min(request.size, kMaxViewportBytes);
                   }
                   dirty_ = true;
                 },
             },
             command);
}

void SimulationController::RebuildBreakpoints() {
  breakpoints_ = std::unordered_set<uint64_t>(address_breakpoints_.begin(),
                                              address_breakpoints_.end());
  if (program_) {
    for (const int line : line_breakpoints_) {
      if (auto address = program_->source_map.AddressForLine(line)) {
        breakpoints_.insert(*address);
      }
    }
  }
  first_chunk_ = true;  // Don't stop on the breakpoint we're paused at.
  dirty_ = true;
}

bool SimulationController::CanEdit() const {
  return program_ && state_ != SimState::kRunning;
}

void SimulationController::Stop(SimState state, StopReason reason) {
  state_ = state;
  stop_reason_ = reason;
  dirty_ = true;
  switch (state) {
    case SimState::kFaulted:
      message_ = machine_.fault_message();
      break;
    case SimState::kExited:
      message_ =
          std::format("Program exited with code {}", machine_.exit_code());
      break;
    default:
      if (reason != StopReason::kAssemblyFailed) message_.clear();
      break;
  }
}

void SimulationController::DoAssemble(AssembleCmd& cmd) {
  if (!(cmd.config == config_)) {
    config_ = cmd.config;
    machine_.Configure(config_);
  }
  AssemblyResult result =
      rvsim::Assemble(cmd.source, AssemblerOptions{.xlen = config_.xlen});
  diagnostics_ = std::move(result.diagnostics);
  console_.Reset();
  ++program_generation_;
  if (!result.ok()) {
    program_.reset();
    machine_.Unload();
    prev_cpu_ = machine_.state();
    const size_t errors = std::ranges::count(diagnostics_, Severity::kError,
                                             &Diagnostic::severity);
    message_ = std::format("Assembly failed with {} error{}", errors,
                           errors == 1 ? "" : "s");
    Stop(SimState::kNoProgram, StopReason::kAssemblyFailed);
    return;
  }
  auto program = std::make_shared<const ProgramImage>(std::move(*result.image));
  if (auto loaded = machine_.Load(*program); !loaded) {
    program_.reset();
    message_ = loaded.error().message;
    diagnostics_.push_back(
        Diagnostic{Severity::kError, 1, 1, loaded.error().message});
    Stop(SimState::kNoProgram, StopReason::kAssemblyFailed);
    return;
  }
  program_ = std::move(program);
  prev_cpu_ = machine_.state();
  RebuildBreakpoints();
  Stop(SimState::kPaused, StopReason::kAssembled);
}

void SimulationController::DoStep() {
  prev_cpu_ = machine_.state();
  const StepResult result = machine_.Step();
  switch (result.status) {
    case StepStatus::kRetired:
      Stop(SimState::kPaused, StopReason::kStep);
      break;
    case StepStatus::kEbreak:
      Stop(SimState::kPaused, StopReason::kEbreak);
      break;
    case StepStatus::kWaitingForInput:
      resume_mode_ = ResumeMode::kStep;
      Stop(SimState::kWaitingForInput, StopReason::kWaitingForInput);
      break;
    case StepStatus::kExited:
      Stop(SimState::kExited, StopReason::kExited);
      break;
    case StepStatus::kFaulted:
      Stop(SimState::kFaulted, StopReason::kFaulted);
      break;
    case StepStatus::kNotRunnable:
      dirty_ = true;
      break;
  }
}

void SimulationController::StartRunning() {
  prev_cpu_ = machine_.state();
  state_ = SimState::kRunning;
  stop_reason_ = StopReason::kNone;
  message_.clear();
  first_chunk_ = true;
  run_start_ = Clock::now();
  run_start_instret_ = machine_.state().instret;
  last_publish_instret_ = machine_.state().instret;
  effective_hz_ = 0.0;
  dirty_ = true;
}

void SimulationController::ExecuteSlice() {
  const auto slice_start = Clock::now();
  while (state_ == SimState::kRunning) {
    uint64_t steps = kChunkSteps;
    if (speed_hz_ != 0) {
      const double elapsed =
          std::chrono::duration<double>(Clock::now() - run_start_).count();
      const auto target =
          static_cast<uint64_t>(elapsed * static_cast<double>(speed_hz_));
      const uint64_t executed = machine_.state().instret - run_start_instret_;
      if (target <= executed) {
        return;  // Nothing due yet; the worker sleeps until the next step.
      }
      steps = std::min<uint64_t>(target - executed, kChunkSteps);
    }
    const RunResult result = machine_.Run(steps, &breakpoints_, first_chunk_);
    first_chunk_ = false;
    HandleRunResult(result);
    if (Clock::now() - slice_start >= kSliceBudget) {
      return;
    }
    // Yield to pending commands (e.g. Pause) promptly.
    std::lock_guard lock(mutex_);
    if (!queue_.empty() || stop_requested_) {
      return;
    }
  }
}

void SimulationController::HandleRunResult(const RunResult& result) {
  if (result.hit_breakpoint) {
    Stop(SimState::kPaused, StopReason::kBreakpoint);
    return;
  }
  switch (result.status) {
    case StepStatus::kRetired:
      break;  // Chunk finished; keep running.
    case StepStatus::kEbreak:
      Stop(SimState::kPaused, StopReason::kEbreak);
      break;
    case StepStatus::kWaitingForInput:
      resume_mode_ = ResumeMode::kRun;
      Stop(SimState::kWaitingForInput, StopReason::kWaitingForInput);
      break;
    case StepStatus::kExited:
      Stop(SimState::kExited, StopReason::kExited);
      break;
    case StepStatus::kFaulted:
      Stop(SimState::kFaulted, StopReason::kFaulted);
      break;
    case StepStatus::kNotRunnable:
      Stop(SimState::kPaused, StopReason::kPaused);
      break;
  }
}

void SimulationController::Publish() {
  auto snap = std::make_shared<UiSnapshot>();
  const auto now = Clock::now();
  const uint64_t instret = machine_.state().instret;
  if (state_ == SimState::kRunning) {
    const double dt =
        std::chrono::duration<double>(now - last_publish_).count();
    if (dt > 0.0 && instret >= last_publish_instret_) {
      effective_hz_ = static_cast<double>(instret - last_publish_instret_) / dt;
    }
  }
  console_.TrimOutput(kMaxConsoleBytes * 2);

  snap->version = ++version_;
  snap->program_generation = program_generation_;
  snap->state = state_;
  snap->stop_reason = stop_reason_;
  snap->message = message_;
  snap->config = config_;
  snap->cpu = machine_.state();
  snap->prev_cpu = prev_cpu_;
  snap->program = program_;
  snap->diagnostics = diagnostics_;
  const std::string& console = console_.combined_text();
  if (console.size() > kMaxConsoleBytes) {
    snap->console = console.substr(console.size() - kMaxConsoleBytes);
    snap->console_truncated = true;
  } else {
    snap->console = console;
  }
  snap->memory.reserve(viewports_.size());
  const PagedRam& ram = machine_.bus().ram();
  for (const MemoryRequest& request : viewports_) {
    MemorySlice slice;
    slice.address = request.address;
    slice.bytes.resize(request.size);
    slice.mapped.resize(request.size);
    ram.ReadRegion(request.address, slice.bytes);
    uint32_t i = 0;
    while (i < request.size) {
      const uint64_t addr = request.address + i;
      const auto in_page = static_cast<uint32_t>(std::min<uint64_t>(
          PagedRam::kPageSize - PagedRam::PageOffset(addr), request.size - i));
      std::fill_n(slice.mapped.begin() + i, in_page,
                  ram.IsMapped(addr) ? uint8_t{1} : uint8_t{0});
      i += in_page;
    }
    snap->memory.push_back(std::move(slice));
  }
  snap->speed_hz = speed_hz_;
  snap->effective_hz = state_ == SimState::kRunning ? effective_hz_ : 0.0;
  snap->exit_code = machine_.exit_code();

  if (state_ == SimState::kRunning && instret != last_publish_instret_) {
    // While running, highlight what changed since the previous frame.
    prev_cpu_ = machine_.state();
  }
  last_publish_ = now;
  last_publish_instret_ = instret;
  dirty_ = false;
  {
    std::lock_guard lock(mutex_);
    snapshot_ = std::move(snap);
  }
  publish_cv_.notify_all();
}

}  // namespace rvsim
