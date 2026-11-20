#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace rvsim {

// Every instruction the simulator understands. RV64-only entries are marked
// in their OpcodeInfo.
enum class Opcode : uint8_t {
  kInvalid,
  // RV32I
  kLui,
  kAuipc,
  kJal,
  kJalr,
  kBeq,
  kBne,
  kBlt,
  kBge,
  kBltu,
  kBgeu,
  kLb,
  kLh,
  kLw,
  kLbu,
  kLhu,
  kSb,
  kSh,
  kSw,
  kAddi,
  kSlti,
  kSltiu,
  kXori,
  kOri,
  kAndi,
  kSlli,
  kSrli,
  kSrai,
  kAdd,
  kSub,
  kSll,
  kSlt,
  kSltu,
  kXor,
  kSrl,
  kSra,
  kOr,
  kAnd,
  kFence,
  kEcall,
  kEbreak,
  // RV64I
  kLwu,
  kLd,
  kSd,
  kAddiw,
  kSlliw,
  kSrliw,
  kSraiw,
  kAddw,
  kSubw,
  kSllw,
  kSrlw,
  kSraw,
  kCount,
};

// Operand layout of an instruction, shared by the decoder, encoder,
// disassembler, and assembler.
enum class InstFormat : uint8_t {
  kNone,      // invalid
  kR,         // op rd, rs1, rs2
  kI,         // op rd, rs1, imm
  kShiftImm,  // op rd, rs1, shamt
  kLoad,      // op rd, imm(rs1)
  kStore,     // op rs2, imm(rs1)
  kBranch,    // op rs1, rs2, target
  kU,         // op rd, imm20
  kJal,       // op rd, target
  kJalr,      // op rd, imm(rs1)
  kFence,     // op pred, succ
  kSystem,    // op
};

struct OpcodeInfo {
  std::string_view mnemonic;
  InstFormat format;
  uint32_t match;  // Fixed opcode/funct3/funct7 bits.
  bool rv64_only;
};

const OpcodeInfo& GetOpcodeInfo(Opcode op);

// Looks up a real (non-pseudo) instruction mnemonic such as "addi".
std::optional<Opcode> LookupMnemonic(std::string_view mnemonic);

// A fully decoded instruction. `imm` is already sign-extended; for U-type
// instructions it holds the final 32-bit value (imm20 << 12, sign-extended).
// For FENCE it holds the raw fm/pred/succ field (bits 31:20).
struct DecodedInst {
  Opcode op = Opcode::kInvalid;
  uint8_t rd = 0;
  uint8_t rs1 = 0;
  uint8_t rs2 = 0;
  uint8_t length = 4;
  int64_t imm = 0;
  uint32_t raw = 0;

  constexpr bool valid() const { return op != Opcode::kInvalid; }
  constexpr bool operator==(const DecodedInst&) const = default;
};

}  // namespace rvsim
