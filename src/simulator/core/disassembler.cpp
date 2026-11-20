#include "simulator/core/disassembler.h"

#include <format>

#include "simulator/core/registers.h"

namespace rvsim {
namespace {

std::string FenceSet(uint32_t bits) {
  std::string out;
  if (bits & 0x8) out += 'i';
  if (bits & 0x4) out += 'o';
  if (bits & 0x2) out += 'r';
  if (bits & 0x1) out += 'w';
  return out.empty() ? "0" : out;
}

}  // namespace

std::string DisassembleOperands(const DecodedInst& inst, uint64_t pc,
                                const DisasmOptions& options) {
  const bool abi = options.abi_names;
  const auto rd = GprName(inst.rd, abi);
  const auto rs1 = GprName(inst.rs1, abi);
  const auto rs2 = GprName(inst.rs2, abi);
  const uint64_t target = pc + static_cast<uint64_t>(inst.imm);

  switch (GetOpcodeInfo(inst.op).format) {
    case InstFormat::kR:
      return std::format("{}, {}, {}", rd, rs1, rs2);
    case InstFormat::kI:
    case InstFormat::kShiftImm:
      return std::format("{}, {}, {}", rd, rs1, inst.imm);
    case InstFormat::kLoad:
    case InstFormat::kJalr:
      return std::format("{}, {}({})", rd, inst.imm, rs1);
    case InstFormat::kStore:
      return std::format("{}, {}({})", rs2, inst.imm, rs1);
    case InstFormat::kBranch:
      return std::format("{}, {}, {:#x}", rs1, rs2, target);
    case InstFormat::kU:
      return std::format("{}, {:#x}", rd, (inst.imm >> 12) & 0xFFFFF);
    case InstFormat::kJal:
      return std::format("{}, {:#x}", rd, target);
    case InstFormat::kFence: {
      const auto field = static_cast<uint32_t>(inst.imm);
      return std::format("{}, {}", FenceSet((field >> 4) & 0xF),
                         FenceSet(field & 0xF));
    }
    case InstFormat::kSystem:
    case InstFormat::kNone:
      break;
  }
  return {};
}

std::string Disassemble(const DecodedInst& inst, uint64_t pc,
                        const DisasmOptions& options) {
  if (!inst.valid()) {
    return std::format(".word {:#010x}", inst.raw);
  }
  const OpcodeInfo& info = GetOpcodeInfo(inst.op);
  if (inst.op == Opcode::kFence && ((inst.imm >> 8) & 0xF) == 0x8) {
    return "fence.tso";
  }
  std::string operands = DisassembleOperands(inst, pc, options);
  if (operands.empty()) {
    return std::string(info.mnemonic);
  }
  return std::format("{} {}", info.mnemonic, operands);
}

}  // namespace rvsim
