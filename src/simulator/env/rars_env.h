#pragma once

#include <cstdint>
#include <string_view>

#include "simulator/env/environment.h"

namespace rvsim {

// RARS / Venus compatible ecall ABI (service number in a7):
//   1 PrintInt      4 PrintString   5 ReadInt      8 ReadString
//   9 Sbrk         10 Exit         11 PrintChar   12 ReadChar
//  34 PrintHex     35 PrintBin     36 PrintUnsigned  93 Exit2 (code in a0)
class RarsEnvironment final : public ExecutionEnvironment {
 public:
  enum Service : uint32_t {
    kPrintInt = 1,
    kPrintString = 4,
    kReadInt = 5,
    kReadString = 8,
    kSbrk = 9,
    kExit = 10,
    kPrintChar = 11,
    kReadChar = 12,
    kPrintHex = 34,
    kPrintBin = 35,
    kPrintUnsigned = 36,
    kExit2 = 93,
  };

  // Upper bound on the length of a PrintString argument.
  static constexpr uint64_t kMaxStringLength = 1u << 20;

  explicit RarsEnvironment(ConsoleIo& console) : console_(console) {}

  std::string_view name() const override { return "RARS/Venus"; }
  void Reset(uint64_t heap_start) override;
  EcallResult HandleEcall(CpuState& state, MemoryBus& bus) override;

  uint64_t program_break() const { return program_break_; }

 private:
  ConsoleIo& console_;
  uint64_t program_break_ = 0;
};

}  // namespace rvsim
