#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rvsim {

inline constexpr int kNumGprs = 32;

// Static description of an integer register.
struct RegisterInfo {
  std::string_view numeric_name;  // "x10"
  std::string_view abi_name;      // "a0"
  std::string_view description;   // "Function argument / return value"
};

inline constexpr std::array<RegisterInfo, kNumGprs> kGprInfo = {{
    {"x0", "zero", "Hard-wired zero"},
    {"x1", "ra", "Return address"},
    {"x2", "sp", "Stack pointer"},
    {"x3", "gp", "Global pointer"},
    {"x4", "tp", "Thread pointer"},
    {"x5", "t0", "Temporary / alternate link register"},
    {"x6", "t1", "Temporary"},
    {"x7", "t2", "Temporary"},
    {"x8", "s0", "Saved register / frame pointer"},
    {"x9", "s1", "Saved register"},
    {"x10", "a0", "Function argument / return value"},
    {"x11", "a1", "Function argument / return value"},
    {"x12", "a2", "Function argument"},
    {"x13", "a3", "Function argument"},
    {"x14", "a4", "Function argument"},
    {"x15", "a5", "Function argument"},
    {"x16", "a6", "Function argument"},
    {"x17", "a7", "Function argument / syscall number"},
    {"x18", "s2", "Saved register"},
    {"x19", "s3", "Saved register"},
    {"x20", "s4", "Saved register"},
    {"x21", "s5", "Saved register"},
    {"x22", "s6", "Saved register"},
    {"x23", "s7", "Saved register"},
    {"x24", "s8", "Saved register"},
    {"x25", "s9", "Saved register"},
    {"x26", "s10", "Saved register"},
    {"x27", "s11", "Saved register"},
    {"x28", "t3", "Temporary"},
    {"x29", "t4", "Temporary"},
    {"x30", "t5", "Temporary"},
    {"x31", "t6", "Temporary"},
}};

// Well-known ABI register indices.
namespace reg {
inline constexpr uint8_t kZero = 0;
inline constexpr uint8_t kRa = 1;
inline constexpr uint8_t kSp = 2;
inline constexpr uint8_t kGp = 3;
inline constexpr uint8_t kTp = 4;
inline constexpr uint8_t kT0 = 5;
inline constexpr uint8_t kT1 = 6;
inline constexpr uint8_t kS0 = 8;
inline constexpr uint8_t kA0 = 10;
inline constexpr uint8_t kA1 = 11;
inline constexpr uint8_t kA2 = 12;
inline constexpr uint8_t kA7 = 17;
}  // namespace reg

constexpr std::string_view GprName(uint8_t index, bool abi = true) {
  const RegisterInfo& info = kGprInfo[index & 31];
  return abi ? info.abi_name : info.numeric_name;
}

// Parses "x0".."x31", ABI names ("a0", "sp", ...) and "fp".
constexpr std::optional<uint8_t> ParseGpr(std::string_view name) {
  if (name == "fp") {
    return reg::kS0;
  }
  for (uint8_t i = 0; i < kNumGprs; ++i) {
    if (kGprInfo[i].abi_name == name || kGprInfo[i].numeric_name == name) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace rvsim
