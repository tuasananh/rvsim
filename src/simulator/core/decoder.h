#pragma once

#include <cstdint>

#include "simulator/core/instruction.h"
#include "simulator/core/types.h"

namespace rvsim {

// Decodes one 32-bit instruction word. Returns a DecodedInst with
// op == Opcode::kInvalid for illegal or unsupported encodings (including
// RV64-only instructions when `xlen` is Xlen::k32).
DecodedInst Decode(uint32_t raw, Xlen xlen);

// Immediate extraction helpers (exposed for the encoder tests).
constexpr int64_t ImmI(uint32_t raw) { return static_cast<int32_t>(raw) >> 20; }
constexpr int64_t ImmS(uint32_t raw) {
  return static_cast<int32_t>((raw & 0xFE000000u) | ((raw >> 7) & 0x1F)
                                                        << 20) >>
         20;
}
constexpr int64_t ImmB(uint32_t raw) {
  const uint32_t bits = (raw & 0x80000000u) | ((raw & 0x80u) << 23) |
                        ((raw >> 1) & 0x3F000000u) |
                        ((raw << 12) & 0x00F00000u);
  return static_cast<int32_t>(bits) >> 19;
}
constexpr int64_t ImmU(uint32_t raw) {
  return static_cast<int32_t>(raw & 0xFFFFF000u);
}
constexpr int64_t ImmJ(uint32_t raw) {
  const uint32_t bits = (raw & 0x80000000u) | ((raw & 0x000FF000u) << 11) |
                        ((raw & 0x00100000u) << 2) | ((raw >> 9) & 0x003FF000u);
  return static_cast<int32_t>(bits) >> 11;
}

}  // namespace rvsim
