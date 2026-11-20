#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "simulator/core/cpu_state.h"
#include "simulator/core/types.h"
#include "simulator/memory/memory_bus.h"

namespace rvsim {

// Console I/O endpoint used by execution environments.
//
// Input functions never block: they return std::nullopt when no input is
// available yet, in which case the environment reports kWaitForInput and the
// ecall is retried once input arrives. End-of-file is reported as an empty
// line / -1 character.
class ConsoleIo {
 public:
  virtual ~ConsoleIo() = default;

  virtual void WriteStdout(std::string_view text) = 0;
  virtual void WriteStderr(std::string_view text) = 0;

  // Returns one line including its trailing '\n' (if any).
  virtual std::optional<std::string> ReadLine() = 0;

  // Returns one byte value (0..255) or -1 on end-of-file.
  virtual std::optional<int> ReadChar() = 0;
};

enum class EcallAction : uint8_t {
  kContinue,      // Syscall completed; advance past the ecall.
  kExit,          // Program requested termination with `exit_code`.
  kWaitForInput,  // Retry this ecall once console input is available.
  kFault,         // Unsupported or failed syscall; see `message`.
};

struct EcallResult {
  EcallAction action = EcallAction::kContinue;
  int64_t exit_code = 0;
  std::string message{};
};

// Handles ECALL instructions for a particular ABI (RARS/Venus, Linux, ...).
class ExecutionEnvironment {
 public:
  virtual ~ExecutionEnvironment() = default;

  virtual std::string_view name() const = 0;

  // Called on program load/reset. `heap_start` is the initial program break.
  virtual void Reset(uint64_t heap_start) = 0;

  virtual EcallResult HandleEcall(CpuState& state, MemoryBus& bus) = 0;
};

}  // namespace rvsim
