#pragma once

#include <array>
#include <cstdint>

#include "simulator/core/types.h"

namespace rvsim {

// LR/SC reservation set (used from Checkpoint 5).
struct Reservation {
  bool valid = false;
  uint64_t address = 0;

  constexpr bool operator==(const Reservation&) const = default;
};

// Architectural state of a single hart.
//
// Integer registers are stored as 64-bit values. In RV32 mode every write is
// masked to 32 bits, so the stored value is always the zero-extended 32-bit
// register contents; use ReadSigned() to obtain the sign-extended value.
struct CpuState {
  Xlen xlen = Xlen::k32;
  IsaExtensions extensions{};
  std::array<uint64_t, 32> x{};
  std::array<uint64_t, 32> f{};
  uint64_t pc = 0;
  PrivilegeMode priv = PrivilegeMode::kUser;
  uint64_t cycle = 0;
  uint64_t instret = 0;
  Reservation reservation;

  constexpr uint64_t XlenMask() const {
    return xlen == Xlen::k32 ? 0xFFFF'FFFFULL : ~0ULL;
  }

  constexpr uint64_t ReadReg(uint8_t idx) const { return x[idx & 31]; }

  // Sign-extended view of a register for the current XLEN.
  constexpr int64_t ReadSigned(uint8_t idx) const {
    const uint64_t v = x[idx & 31];
    if (xlen == Xlen::k32) {
      return static_cast<int64_t>(
          static_cast<int32_t>(static_cast<uint32_t>(v)));
    }
    return static_cast<int64_t>(v);
  }

  constexpr void WriteReg(uint8_t idx, uint64_t value) {
    idx &= 31;
    if (idx != 0) {
      x[idx] = value & XlenMask();
    }
  }

  constexpr void SetPc(uint64_t value) { pc = value & XlenMask(); }

  // Clears all registers and counters while keeping configuration.
  constexpr void ResetArchState() {
    x.fill(0);
    f.fill(0);
    pc = 0;
    priv = PrivilegeMode::kUser;
    cycle = 0;
    instret = 0;
    reservation = {};
  }

  constexpr bool operator==(const CpuState&) const = default;
};

}  // namespace rvsim
