#include "simulator/core/decoder.h"

namespace rvsim {
namespace {

constexpr uint32_t kOpLoad = 0x03;
constexpr uint32_t kOpMiscMem = 0x0F;
constexpr uint32_t kOpImm = 0x13;
constexpr uint32_t kOpAuipc = 0x17;
constexpr uint32_t kOpImm32 = 0x1B;
constexpr uint32_t kOpStore = 0x23;
constexpr uint32_t kOpOp = 0x33;
constexpr uint32_t kOpLui = 0x37;
constexpr uint32_t kOpOp32 = 0x3B;
constexpr uint32_t kOpBranch = 0x63;
constexpr uint32_t kOpJalr = 0x67;
constexpr uint32_t kOpJal = 0x6F;
constexpr uint32_t kOpSystem = 0x73;

Opcode DecodeLoad(uint32_t funct3, bool rv64) {
  switch (funct3) {
    case 0:
      return Opcode::kLb;
    case 1:
      return Opcode::kLh;
    case 2:
      return Opcode::kLw;
    case 3:
      return rv64 ? Opcode::kLd : Opcode::kInvalid;
    case 4:
      return Opcode::kLbu;
    case 5:
      return Opcode::kLhu;
    case 6:
      return rv64 ? Opcode::kLwu : Opcode::kInvalid;
    default:
      return Opcode::kInvalid;
  }
}

Opcode DecodeStore(uint32_t funct3, bool rv64) {
  switch (funct3) {
    case 0:
      return Opcode::kSb;
    case 1:
      return Opcode::kSh;
    case 2:
      return Opcode::kSw;
    case 3:
      return rv64 ? Opcode::kSd : Opcode::kInvalid;
    default:
      return Opcode::kInvalid;
  }
}

Opcode DecodeBranch(uint32_t funct3) {
  switch (funct3) {
    case 0:
      return Opcode::kBeq;
    case 1:
      return Opcode::kBne;
    case 4:
      return Opcode::kBlt;
    case 5:
      return Opcode::kBge;
    case 6:
      return Opcode::kBltu;
    case 7:
      return Opcode::kBgeu;
    default:
      return Opcode::kInvalid;
  }
}

Opcode DecodeOpImm(uint32_t raw, uint32_t funct3, bool rv64) {
  // RV64 shifts use a 6-bit shamt (bits 25:20); RV32 requires bit 25 == 0.
  const uint32_t shift_funct = raw >> (rv64 ? 26 : 25);
  const uint32_t shift_funct_expect_srai = rv64 ? 0x10 : 0x20;
  switch (funct3) {
    case 0:
      return Opcode::kAddi;
    case 2:
      return Opcode::kSlti;
    case 3:
      return Opcode::kSltiu;
    case 4:
      return Opcode::kXori;
    case 6:
      return Opcode::kOri;
    case 7:
      return Opcode::kAndi;
    case 1:
      return shift_funct == 0 ? Opcode::kSlli : Opcode::kInvalid;
    case 5:
      if (shift_funct == 0) {
        return Opcode::kSrli;
      }
      return shift_funct == shift_funct_expect_srai ? Opcode::kSrai
                                                    : Opcode::kInvalid;
    default:
      return Opcode::kInvalid;
  }
}

Opcode DecodeOp(uint32_t funct3, uint32_t funct7) {
  if (funct7 == 0x00) {
    switch (funct3) {
      case 0:
        return Opcode::kAdd;
      case 1:
        return Opcode::kSll;
      case 2:
        return Opcode::kSlt;
      case 3:
        return Opcode::kSltu;
      case 4:
        return Opcode::kXor;
      case 5:
        return Opcode::kSrl;
      case 6:
        return Opcode::kOr;
      case 7:
        return Opcode::kAnd;
      default:
        return Opcode::kInvalid;
    }
  }
  if (funct7 == 0x20) {
    switch (funct3) {
      case 0:
        return Opcode::kSub;
      case 5:
        return Opcode::kSra;
      default:
        return Opcode::kInvalid;
    }
  }
  return Opcode::kInvalid;
}

Opcode DecodeOpImm32(uint32_t funct3, uint32_t funct7) {
  switch (funct3) {
    case 0:
      return Opcode::kAddiw;
    case 1:
      return funct7 == 0x00 ? Opcode::kSlliw : Opcode::kInvalid;
    case 5:
      if (funct7 == 0x00) {
        return Opcode::kSrliw;
      }
      return funct7 == 0x20 ? Opcode::kSraiw : Opcode::kInvalid;
    default:
      return Opcode::kInvalid;
  }
}

Opcode DecodeOp32(uint32_t funct3, uint32_t funct7) {
  if (funct7 == 0x00) {
    switch (funct3) {
      case 0:
        return Opcode::kAddw;
      case 1:
        return Opcode::kSllw;
      case 5:
        return Opcode::kSrlw;
      default:
        return Opcode::kInvalid;
    }
  }
  if (funct7 == 0x20) {
    switch (funct3) {
      case 0:
        return Opcode::kSubw;
      case 5:
        return Opcode::kSraw;
      default:
        return Opcode::kInvalid;
    }
  }
  return Opcode::kInvalid;
}

}  // namespace

DecodedInst Decode(uint32_t raw, Xlen xlen) {
  const bool rv64 = xlen == Xlen::k64;
  DecodedInst inst;
  inst.raw = raw;
  inst.length = 4;
  if ((raw & 0x3) != 0x3) {
    return inst;  // Compressed encodings arrive in Checkpoint 6.
  }

  const uint32_t opcode = raw & 0x7F;
  const uint32_t funct3 = (raw >> 12) & 0x7;
  const uint32_t funct7 = raw >> 25;
  const auto rd = static_cast<uint8_t>((raw >> 7) & 0x1F);
  const auto rs1 = static_cast<uint8_t>((raw >> 15) & 0x1F);
  const auto rs2 = static_cast<uint8_t>((raw >> 20) & 0x1F);

  switch (opcode) {
    case kOpLui:
    case kOpAuipc:
      inst.op = opcode == kOpLui ? Opcode::kLui : Opcode::kAuipc;
      inst.rd = rd;
      inst.imm = ImmU(raw);
      break;
    case kOpJal:
      inst.op = Opcode::kJal;
      inst.rd = rd;
      inst.imm = ImmJ(raw);
      break;
    case kOpJalr:
      if (funct3 == 0) {
        inst.op = Opcode::kJalr;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.imm = ImmI(raw);
      }
      break;
    case kOpBranch:
      inst.op = DecodeBranch(funct3);
      inst.rs1 = rs1;
      inst.rs2 = rs2;
      inst.imm = ImmB(raw);
      break;
    case kOpLoad:
      inst.op = DecodeLoad(funct3, rv64);
      inst.rd = rd;
      inst.rs1 = rs1;
      inst.imm = ImmI(raw);
      break;
    case kOpStore:
      inst.op = DecodeStore(funct3, rv64);
      inst.rs1 = rs1;
      inst.rs2 = rs2;
      inst.imm = ImmS(raw);
      break;
    case kOpImm:
      inst.op = DecodeOpImm(raw, funct3, rv64);
      inst.rd = rd;
      inst.rs1 = rs1;
      if (funct3 == 1 || funct3 == 5) {
        inst.imm = (raw >> 20) & (rv64 ? 0x3F : 0x1F);
      } else {
        inst.imm = ImmI(raw);
      }
      break;
    case kOpOp:
      inst.op = DecodeOp(funct3, funct7);
      inst.rd = rd;
      inst.rs1 = rs1;
      inst.rs2 = rs2;
      break;
    case kOpImm32:
      if (rv64) {
        inst.op = DecodeOpImm32(funct3, funct7);
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.imm = funct3 == 0 ? ImmI(raw) : static_cast<int64_t>(rs2);
      }
      break;
    case kOpOp32:
      if (rv64) {
        inst.op = DecodeOp32(funct3, funct7);
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.rs2 = rs2;
      }
      break;
    case kOpMiscMem:
      if (funct3 == 0) {
        inst.op = Opcode::kFence;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.imm = raw >> 20;
      }
      break;
    case kOpSystem:
      if (raw == 0x00000073) {
        inst.op = Opcode::kEcall;
      } else if (raw == 0x00100073) {
        inst.op = Opcode::kEbreak;
      }
      break;
    default:
      break;
  }

  if (inst.op == Opcode::kInvalid) {
    return DecodedInst{.op = Opcode::kInvalid, .length = 4, .raw = raw};
  }
  return inst;
}

}  // namespace rvsim
