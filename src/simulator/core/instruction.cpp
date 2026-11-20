#include "simulator/core/instruction.h"

#include <array>

namespace rvsim {
namespace {

using enum InstFormat;

constexpr std::array<OpcodeInfo, static_cast<size_t>(Opcode::kCount)>
    kOpcodeTable = {{
        {"invalid", kNone, 0, false},
        {"lui", kU, 0x00000037, false},
        {"auipc", kU, 0x00000017, false},
        {"jal", kJal, 0x0000006F, false},
        {"jalr", kJalr, 0x00000067, false},
        {"beq", kBranch, 0x00000063, false},
        {"bne", kBranch, 0x00001063, false},
        {"blt", kBranch, 0x00004063, false},
        {"bge", kBranch, 0x00005063, false},
        {"bltu", kBranch, 0x00006063, false},
        {"bgeu", kBranch, 0x00007063, false},
        {"lb", kLoad, 0x00000003, false},
        {"lh", kLoad, 0x00001003, false},
        {"lw", kLoad, 0x00002003, false},
        {"lbu", kLoad, 0x00004003, false},
        {"lhu", kLoad, 0x00005003, false},
        {"sb", kStore, 0x00000023, false},
        {"sh", kStore, 0x00001023, false},
        {"sw", kStore, 0x00002023, false},
        {"addi", kI, 0x00000013, false},
        {"slti", kI, 0x00002013, false},
        {"sltiu", kI, 0x00003013, false},
        {"xori", kI, 0x00004013, false},
        {"ori", kI, 0x00006013, false},
        {"andi", kI, 0x00007013, false},
        {"slli", kShiftImm, 0x00001013, false},
        {"srli", kShiftImm, 0x00005013, false},
        {"srai", kShiftImm, 0x40005013, false},
        {"add", kR, 0x00000033, false},
        {"sub", kR, 0x40000033, false},
        {"sll", kR, 0x00001033, false},
        {"slt", kR, 0x00002033, false},
        {"sltu", kR, 0x00003033, false},
        {"xor", kR, 0x00004033, false},
        {"srl", kR, 0x00005033, false},
        {"sra", kR, 0x40005033, false},
        {"or", kR, 0x00006033, false},
        {"and", kR, 0x00007033, false},
        {"fence", kFence, 0x0000000F, false},
        {"ecall", kSystem, 0x00000073, false},
        {"ebreak", kSystem, 0x00100073, false},
        {"lwu", kLoad, 0x00006003, true},
        {"ld", kLoad, 0x00003003, true},
        {"sd", kStore, 0x00003023, true},
        {"addiw", kI, 0x0000001B, true},
        {"slliw", kShiftImm, 0x0000101B, true},
        {"srliw", kShiftImm, 0x0000501B, true},
        {"sraiw", kShiftImm, 0x4000501B, true},
        {"addw", kR, 0x0000003B, true},
        {"subw", kR, 0x4000003B, true},
        {"sllw", kR, 0x0000103B, true},
        {"srlw", kR, 0x0000503B, true},
        {"sraw", kR, 0x4000503B, true},
    }};

}  // namespace

const OpcodeInfo& GetOpcodeInfo(Opcode op) {
  const auto index = static_cast<size_t>(op);
  return index < kOpcodeTable.size() ? kOpcodeTable[index] : kOpcodeTable[0];
}

std::optional<Opcode> LookupMnemonic(std::string_view mnemonic) {
  for (size_t i = 1; i < kOpcodeTable.size(); ++i) {
    if (kOpcodeTable[i].mnemonic == mnemonic) {
      return static_cast<Opcode>(i);
    }
  }
  return std::nullopt;
}

}  // namespace rvsim
