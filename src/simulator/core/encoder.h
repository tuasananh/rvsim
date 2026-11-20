#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "simulator/core/instruction.h"
#include "simulator/core/types.h"

namespace rvsim {

// Encodes a DecodedInst back to a 32-bit instruction word, validating the
// register indices and immediate ranges for the instruction's format:
//   I/load/jalr: signed 12-bit, store: signed 12-bit, branch: signed 13-bit
//   even, jal: signed 21-bit even, U: imm must be a sign-extended multiple of
//   4096 (i.e. `imm20 << 12`), shifts: 0..XLEN-1 (0..31 for *W).
// Returns an error message on failure.
std::expected<uint32_t, std::string> Encode(const DecodedInst& inst, Xlen xlen);

// Convenience overload.
inline std::expected<uint32_t, std::string> Encode(Opcode op, uint8_t rd,
                                                   uint8_t rs1, uint8_t rs2,
                                                   int64_t imm, Xlen xlen) {
  return Encode(
      DecodedInst{.op = op, .rd = rd, .rs1 = rs1, .rs2 = rs2, .imm = imm},
      xlen);
}

}  // namespace rvsim
