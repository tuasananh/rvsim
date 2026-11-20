#pragma once

#include <cstdint>
#include <string>

#include "simulator/core/instruction.h"

namespace rvsim {

struct DisasmOptions {
  bool abi_names = true;  // "a0" instead of "x10".
};

// Formats a decoded instruction as canonical RISC-V assembly, e.g.
// "addi a0, sp, -16", "lw a1, 8(s0)", "beq a0, zero, 0x400010".
// Branch and jump targets are printed as absolute addresses relative to `pc`.
std::string Disassemble(const DecodedInst& inst, uint64_t pc,
                        const DisasmOptions& options = {});

// Formats only the operand list (no mnemonic) of a decoded instruction.
std::string DisassembleOperands(const DecodedInst& inst, uint64_t pc,
                                const DisasmOptions& options = {});

}  // namespace rvsim
