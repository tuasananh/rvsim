#pragma once

#include <cstdint>

#include "simulator/core/cpu_state.h"
#include "simulator/core/instruction.h"
#include "simulator/core/types.h"
#include "simulator/memory/memory_bus.h"

namespace rvsim {

enum class ExecStatus : uint8_t {
  kOk,      // Instruction retired; pc advanced.
  kEcall,   // ECALL reached; pc still points at the ecall.
  kEbreak,  // EBREAK reached; pc still points at the ebreak.
  kTrap,    // Synchronous exception; architectural state is unchanged.
};

struct ExecResult {
  ExecStatus status = ExecStatus::kOk;
  Trap trap{};
};

// Executes one decoded instruction with exact RISC-V semantics for the given
// XLEN. On kTrap, kEcall, and kEbreak neither registers, memory, nor pc are
// modified; the caller decides how to continue.
template <Xlen kXlen>
ExecResult Execute(CpuState& state, MemoryBus& bus, const DecodedInst& inst);

// Dispatches to Execute<Xlen::k32> or Execute<Xlen::k64> based on state.xlen.
ExecResult Execute(CpuState& state, MemoryBus& bus, const DecodedInst& inst);

extern template ExecResult Execute<Xlen::k32>(CpuState&, MemoryBus&,
                                              const DecodedInst&);
extern template ExecResult Execute<Xlen::k64>(CpuState&, MemoryBus&,
                                              const DecodedInst&);

}  // namespace rvsim
