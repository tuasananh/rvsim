#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace rvsim {

// Native register width of the simulated hart.
enum class Xlen : uint8_t { k32 = 32, k64 = 64 };

constexpr std::string_view ToString(Xlen xlen) {
  return xlen == Xlen::k32 ? "RV32" : "RV64";
}

// Bitmask of enabled ISA extensions beyond the base integer ISA.
enum class Extension : uint32_t {
  kNone = 0,
  kM = 1u << 0,
  kA = 1u << 1,
  kF = 1u << 2,
  kD = 1u << 3,
  kC = 1u << 4,
  kZicsr = 1u << 5,
  kZifencei = 1u << 6,
};

class IsaExtensions {
 public:
  constexpr IsaExtensions() = default;
  constexpr explicit IsaExtensions(uint32_t mask) : mask_(mask) {}

  constexpr bool Has(Extension ext) const {
    return (mask_ & static_cast<uint32_t>(ext)) != 0;
  }
  constexpr IsaExtensions& Enable(Extension ext) {
    mask_ |= static_cast<uint32_t>(ext);
    return *this;
  }
  constexpr uint32_t mask() const { return mask_; }

  constexpr bool operator==(const IsaExtensions&) const = default;

 private:
  uint32_t mask_ = 0;
};

enum class PrivilegeMode : uint8_t { kUser = 0, kSupervisor = 1, kMachine = 3 };

// Synchronous exception causes (mcause encodings from the privileged spec).
enum class TrapCause : uint8_t {
  kInstructionAddressMisaligned = 0,
  kInstructionAccessFault = 1,
  kIllegalInstruction = 2,
  kBreakpoint = 3,
  kLoadAddressMisaligned = 4,
  kLoadAccessFault = 5,
  kStoreAddressMisaligned = 6,
  kStoreAccessFault = 7,
  kEcallFromUMode = 8,
  kEcallFromSMode = 9,
  kEcallFromMMode = 11,
};

constexpr std::string_view ToString(TrapCause cause) {
  switch (cause) {
    case TrapCause::kInstructionAddressMisaligned:
      return "instruction address misaligned";
    case TrapCause::kInstructionAccessFault:
      return "instruction access fault";
    case TrapCause::kIllegalInstruction:
      return "illegal instruction";
    case TrapCause::kBreakpoint:
      return "breakpoint";
    case TrapCause::kLoadAddressMisaligned:
      return "load address misaligned";
    case TrapCause::kLoadAccessFault:
      return "load access fault";
    case TrapCause::kStoreAddressMisaligned:
      return "store address misaligned";
    case TrapCause::kStoreAccessFault:
      return "store access fault";
    case TrapCause::kEcallFromUMode:
      return "environment call from U-mode";
    case TrapCause::kEcallFromSMode:
      return "environment call from S-mode";
    case TrapCause::kEcallFromMMode:
      return "environment call from M-mode";
  }
  return "unknown trap";
}

// An architectural trap: cause plus the value that would be written to mtval.
struct Trap {
  TrapCause cause = TrapCause::kIllegalInstruction;
  uint64_t tval = 0;

  constexpr bool operator==(const Trap&) const = default;
};

enum class ErrorCode : uint8_t {
  kInvalidArgument,
  kParseError,
  kLoadError,
  kUnsupported,
  kInternal,
};

// Generic error value for fallible host-level operations (no exceptions).
struct Error {
  ErrorCode code = ErrorCode::kInternal;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> MakeError(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

}  // namespace rvsim
