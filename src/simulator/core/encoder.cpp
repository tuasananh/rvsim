#include "simulator/core/encoder.h"

#include <format>

namespace rvsim {
namespace {

bool FitsSigned(int64_t value, int bits) {
  const int64_t min = -(int64_t{1} << (bits - 1));
  const int64_t max = (int64_t{1} << (bits - 1)) - 1;
  return value >= min && value <= max;
}

std::unexpected<std::string> RangeError(std::string_view what, int64_t value,
                                        int64_t min, int64_t max) {
  return std::unexpected(
      std::format("{} {} out of range [{}, {}]", what, value, min, max));
}

uint32_t Rd(uint8_t r) { return static_cast<uint32_t>(r & 31) << 7; }
uint32_t Rs1(uint8_t r) { return static_cast<uint32_t>(r & 31) << 15; }
uint32_t Rs2(uint8_t r) { return static_cast<uint32_t>(r & 31) << 20; }

}  // namespace

std::expected<uint32_t, std::string> Encode(const DecodedInst& inst,
                                            Xlen xlen) {
  const OpcodeInfo& info = GetOpcodeInfo(inst.op);
  if (info.format == InstFormat::kNone) {
    return std::unexpected("invalid instruction");
  }
  if (info.rv64_only && xlen != Xlen::k64) {
    return std::unexpected(std::format("'{}' requires RV64", info.mnemonic));
  }
  if (inst.rd > 31 || inst.rs1 > 31 || inst.rs2 > 31) {
    return std::unexpected("register index out of range");
  }

  const int64_t imm = inst.imm;
  const uint32_t match = info.match;
  switch (info.format) {
    case InstFormat::kR:
      return match | Rd(inst.rd) | Rs1(inst.rs1) | Rs2(inst.rs2);
    case InstFormat::kI:
    case InstFormat::kLoad:
    case InstFormat::kJalr:
      if (!FitsSigned(imm, 12)) {
        return RangeError("immediate", imm, -2048, 2047);
      }
      return match | Rd(inst.rd) | Rs1(inst.rs1) |
             (static_cast<uint32_t>(imm & 0xFFF) << 20);
    case InstFormat::kShiftImm: {
      const bool word_op = inst.op == Opcode::kSlliw ||
                           inst.op == Opcode::kSrliw ||
                           inst.op == Opcode::kSraiw;
      const int64_t max = (xlen == Xlen::k64 && !word_op) ? 63 : 31;
      if (imm < 0 || imm > max) {
        return RangeError("shift amount", imm, 0, max);
      }
      return match | Rd(inst.rd) | Rs1(inst.rs1) |
             (static_cast<uint32_t>(imm) << 20);
    }
    case InstFormat::kStore:
      if (!FitsSigned(imm, 12)) {
        return RangeError("immediate", imm, -2048, 2047);
      }
      return match | Rs1(inst.rs1) | Rs2(inst.rs2) |
             (static_cast<uint32_t>((imm >> 5) & 0x7F) << 25) |
             (static_cast<uint32_t>(imm & 0x1F) << 7);
    case InstFormat::kBranch: {
      if (!FitsSigned(imm, 13)) {
        return RangeError("branch offset", imm, -4096, 4094);
      }
      if ((imm & 1) != 0) {
        return std::unexpected(
            std::format("branch offset {} is not a multiple of 2", imm));
      }
      const auto u = static_cast<uint32_t>(imm);
      return match | Rs1(inst.rs1) | Rs2(inst.rs2) | (((u >> 12) & 1) << 31) |
             (((u >> 5) & 0x3F) << 25) | (((u >> 1) & 0xF) << 8) |
             (((u >> 11) & 1) << 7);
    }
    case InstFormat::kU: {
      if ((imm & 0xFFF) != 0 || !FitsSigned(imm, 32)) {
        return std::unexpected(std::format(
            "upper immediate {:#x} is not a 20-bit value shifted by 12", imm));
      }
      return match | Rd(inst.rd) | (static_cast<uint32_t>(imm) & 0xFFFFF000u);
    }
    case InstFormat::kJal: {
      if (!FitsSigned(imm, 21)) {
        return RangeError("jump offset", imm, -(1 << 20), (1 << 20) - 2);
      }
      if ((imm & 1) != 0) {
        return std::unexpected(
            std::format("jump offset {} is not a multiple of 2", imm));
      }
      const auto u = static_cast<uint32_t>(imm);
      return match | Rd(inst.rd) | (((u >> 20) & 1) << 31) |
             (((u >> 1) & 0x3FF) << 21) | (((u >> 11) & 1) << 20) |
             (((u >> 12) & 0xFF) << 12);
    }
    case InstFormat::kFence:
      if (imm < 0 || imm > 0xFFF) {
        return RangeError("fence field", imm, 0, 0xFFF);
      }
      return match | Rd(inst.rd) | Rs1(inst.rs1) |
             (static_cast<uint32_t>(imm) << 20);
    case InstFormat::kSystem:
      return match;
    case InstFormat::kNone:
      break;
  }
  return std::unexpected("invalid instruction");
}

}  // namespace rvsim
