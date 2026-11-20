#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "simulator/core/cpu_state.h"
#include "simulator/core/types.h"
#include "simulator/env/environment.h"
#include "simulator/loader/program_image.h"
#include "simulator/memory/memory_bus.h"

namespace rvsim {

enum class EnvironmentKind : uint8_t { kRars };

constexpr std::string_view ToString(EnvironmentKind kind) {
  switch (kind) {
    case EnvironmentKind::kRars:
      return "RARS";
  }
  return "?";
}

struct MachineConfig {
  Xlen xlen = Xlen::k32;
  IsaExtensions extensions{};
  EnvironmentKind environment = EnvironmentKind::kRars;
  bool strict_alignment = false;
  size_t max_pages = PagedRam::kDefaultMaxPages;

  bool operator==(const MachineConfig&) const = default;
};

enum class RunState : uint8_t {
  kNoProgram,  // Nothing loaded yet.
  kReady,      // Can execute instructions.
  kExited,     // Program terminated through an exit ecall.
  kFaulted,    // Unhandled trap or failed ecall.
};

constexpr std::string_view ToString(RunState state) {
  switch (state) {
    case RunState::kNoProgram:
      return "No program";
    case RunState::kReady:
      return "Ready";
    case RunState::kExited:
      return "Exited";
    case RunState::kFaulted:
      return "Faulted";
  }
  return "?";
}

enum class StepStatus : uint8_t {
  kRetired,          // One instruction retired.
  kEbreak,           // EBREAK retired; execution should pause.
  kWaitingForInput,  // Blocked on console input; nothing retired.
  kExited,           // Program exited.
  kFaulted,          // Unhandled trap or failed ecall.
  kNotRunnable,      // Machine is not in the Ready state.
};

struct StepResult {
  StepStatus status = StepStatus::kRetired;
};

struct RunResult {
  StepStatus status = StepStatus::kRetired;
  uint64_t steps = 0;
  bool hit_breakpoint = false;
};

// Synchronous single-hart machine: CPU state + memory + execution
// environment. Not thread-safe; see SimulationController for threading.
class Machine {
 public:
  Machine(const MachineConfig& config, ConsoleIo& console);
  ~Machine();

  Machine(const Machine&) = delete;
  Machine& operator=(const Machine&) = delete;

  // Applies a new configuration and unloads the current program.
  void Configure(const MachineConfig& config);
  const MachineConfig& config() const { return config_; }

  // Loads a program image (its XLEN must match the configuration) and resets.
  Result<void> Load(ProgramImage image);
  // Restores memory and registers to the freshly loaded program state.
  void Reset();
  void Unload();

  StepResult Step();

  // Executes up to `max_steps` instructions, stopping early on exit, fault,
  // ebreak, input wait, or before executing an address in `breakpoints`.
  // When `skip_breakpoint_at_start` is true the first instruction executes
  // even if it has a breakpoint, so a run can resume from a breakpoint; pass
  // false when continuing a run that was split into chunks.
  RunResult Run(uint64_t max_steps,
                const std::unordered_set<uint64_t>* breakpoints = nullptr,
                bool skip_breakpoint_at_start = true);

  CpuState& state() { return state_; }
  const CpuState& state() const { return state_; }
  MemoryBus& bus() { return *bus_; }
  const MemoryBus& bus() const { return *bus_; }
  ExecutionEnvironment& environment() { return *environment_; }

  bool has_program() const { return image_.has_value(); }
  const ProgramImage* image() const { return image_ ? &*image_ : nullptr; }

  RunState run_state() const { return run_state_; }
  int64_t exit_code() const { return exit_code_; }
  const std::optional<Trap>& last_trap() const { return last_trap_; }
  // Human-readable description of why the machine faulted.
  const std::string& fault_message() const { return fault_message_; }

 private:
  void CreateDevices();
  void Fault(std::string message, std::optional<Trap> trap);

  MachineConfig config_;
  ConsoleIo& console_;
  CpuState state_;
  std::unique_ptr<MemoryBus> bus_;
  std::unique_ptr<ExecutionEnvironment> environment_;
  std::optional<ProgramImage> image_;
  RunState run_state_ = RunState::kNoProgram;
  int64_t exit_code_ = 0;
  std::optional<Trap> last_trap_;
  std::string fault_message_;
};

}  // namespace rvsim
