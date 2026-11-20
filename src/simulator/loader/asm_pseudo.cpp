#include "simulator/loader/asm_pseudo.h"

#include <format>
#include <limits>

#include "simulator/core/registers.h"

namespace rvsim {

bool Expr::HasSymbols() const {
  switch (kind) {
    case Kind::kNumber:
      return false;
    case Kind::kSymbol:
    case Kind::kLocalLabel:
    case Kind::kDot:
      return true;
    case Kind::kUnary:
    case Kind::kReloc:
      return lhs && lhs->HasSymbols();
    case Kind::kBinary:
      return (lhs && lhs->HasSymbols()) || (rhs && rhs->HasSymbols());
  }
  return false;
}

ExprPtr MakeNumber(int64_t value, int column) {
  auto expr = std::make_shared<Expr>();
  expr->kind = Expr::Kind::kNumber;
  expr->number = value;
  expr->column = column;
  return expr;
}

namespace {

using enum Opcode;

constexpr int64_t SignExtend12(int64_t value) {
  return ((value & 0xFFF) ^ 0x800) - 0x800;
}

PendingInst Make(Opcode op, uint8_t rd, uint8_t rs1, uint8_t rs2,
                 int64_t imm = 0) {
  return PendingInst{.op = op, .rd = rd, .rs1 = rs1, .rs2 = rs2, .imm = imm};
}

// Immediate operand flavours accepted by different instruction slots.
enum class ImmSlot : uint8_t { kLow12, kUpper, kAuipcUpper, kTarget, kPlain };

struct ImmSpec {
  ImmMode mode = ImmMode::kConstant;
  ExprPtr expr{};
};

std::string_view RelocName(RelocKind kind) {
  switch (kind) {
    case RelocKind::kHi:
      return "%hi";
    case RelocKind::kLo:
      return "%lo";
    case RelocKind::kPcrelHi:
      return "%pcrel_hi";
    case RelocKind::kPcrelLo:
      return "%pcrel_lo";
    case RelocKind::kNone:
      break;
  }
  return "";
}

std::expected<ImmSpec, AsmError> ClassifyImm(const ExprPtr& expr, ImmSlot slot,
                                             int column) {
  if (!expr) {
    return ImmSpec{ImmMode::kConstant, nullptr};
  }
  if (expr->kind == Expr::Kind::kReloc) {
    const RelocKind reloc = expr->reloc;
    switch (slot) {
      case ImmSlot::kLow12:
        if (reloc == RelocKind::kLo) return ImmSpec{ImmMode::kLo, expr->lhs};
        if (reloc == RelocKind::kPcrelLo) {
          return ImmSpec{ImmMode::kPcrelLo, expr->lhs};
        }
        break;
      case ImmSlot::kUpper:
        if (reloc == RelocKind::kHi) return ImmSpec{ImmMode::kHi, expr->lhs};
        break;
      case ImmSlot::kAuipcUpper:
        if (reloc == RelocKind::kHi) return ImmSpec{ImmMode::kHi, expr->lhs};
        if (reloc == RelocKind::kPcrelHi) {
          return ImmSpec{ImmMode::kPcrelHi, expr->lhs};
        }
        break;
      case ImmSlot::kTarget:
      case ImmSlot::kPlain:
        break;
    }
    return std::unexpected(AsmError{
        std::format("{} is not valid in this operand", RelocName(reloc)),
        column});
  }
  switch (slot) {
    case ImmSlot::kUpper:
    case ImmSlot::kAuipcUpper:
      return ImmSpec{ImmMode::kUpperValue, expr};
    case ImmSlot::kTarget:
      return ImmSpec{expr->HasSymbols() ? ImmMode::kPcRel : ImmMode::kValue,
                     expr};
    case ImmSlot::kLow12:
    case ImmSlot::kPlain:
      break;
  }
  return ImmSpec{ImmMode::kValue, expr};
}

PendingInst WithImm(PendingInst inst, const ImmSpec& spec) {
  inst.mode = spec.mode;
  inst.expr = spec.expr;
  if (spec.mode == ImmMode::kConstant) {
    inst.imm = 0;
  }
  return inst;
}

std::optional<Opcode> ImmediateForm(Opcode op) {
  switch (op) {
    case kAdd:
      return kAddi;
    case kAnd:
      return kAndi;
    case kOr:
      return kOri;
    case kXor:
      return kXori;
    case kSll:
      return kSlli;
    case kSrl:
      return kSrli;
    case kSra:
      return kSrai;
    case kSlt:
      return kSlti;
    case kSltu:
      return kSltiu;
    case kAddw:
      return kAddiw;
    case kSllw:
      return kSlliw;
    case kSrlw:
      return kSrliw;
    case kSraw:
      return kSraiw;
    default:
      return std::nullopt;
  }
}

std::optional<int64_t> ParseFenceSet(std::string_view text) {
  int64_t bits = 0;
  if (text == "0") return 0;
  for (const char c : text) {
    int64_t bit = 0;
    switch (c) {
      case 'i':
        bit = 8;
        break;
      case 'o':
        bit = 4;
        break;
      case 'r':
        bit = 2;
        break;
      case 'w':
        bit = 1;
        break;
      default:
        return std::nullopt;
    }
    if (bits & bit) return std::nullopt;
    bits |= bit;
  }
  return bits;
}

class Expander {
 public:
  Expander(std::string_view mnemonic, std::span<const Operand> ops,
           const ExpandContext& context)
      : mnemonic_(mnemonic), ops_(ops), ctx_(context) {}

  std::expected<std::vector<PendingInst>, AsmError> Run() {
    auto result = Dispatch();
    if (!result) {
      return std::unexpected(result.error());
    }
    for (PendingInst& inst : out_) {
      if (inst.column == 0) inst.column = ctx_.mnemonic_column;
      if (GetOpcodeInfo(inst.op).rv64_only && ctx_.xlen != Xlen::k64) {
        return std::unexpected(
            AsmError{std::format("'{}' requires RV64", mnemonic_),
                     ctx_.mnemonic_column});
      }
    }
    return std::move(out_);
  }

 private:
  using Status = std::expected<void, AsmError>;

  std::unexpected<AsmError> Fail(std::string message, int column = 0) const {
    return std::unexpected(
        AsmError{std::move(message), column ? column : ctx_.mnemonic_column});
  }

  Status Count(size_t expected) const {
    if (ops_.size() != expected) {
      return Fail(std::format("'{}' expects {} operand{}, got {}", mnemonic_,
                              expected, expected == 1 ? "" : "s", ops_.size()));
    }
    return {};
  }

  std::expected<uint8_t, AsmError> Reg(size_t index) const {
    const Operand& op = ops_[index];
    if (op.kind != Operand::Kind::kRegister) {
      return Fail(std::format("operand {} of '{}' must be a register",
                              index + 1, mnemonic_),
                  op.column);
    }
    return op.reg;
  }

  std::expected<ImmSpec, AsmError> Imm(size_t index, ImmSlot slot) const {
    const Operand& op = ops_[index];
    if (op.kind != Operand::Kind::kExpression) {
      return Fail(std::format("operand {} of '{}' must be an immediate",
                              index + 1, mnemonic_),
                  op.column);
    }
    return ClassifyImm(op.expr, slot, op.column);
  }

  void Emit(PendingInst inst, int column = 0) {
    inst.column = column;
    out_.push_back(std::move(inst));
  }

  // auipc tmp, %pcrel_hi(expr); <second> using %pcrel_lo of that auipc.
  void EmitPcrelPair(uint8_t tmp, const ExprPtr& target, PendingInst second,
                     int column) {
    Emit(WithImm(Make(kAuipc, tmp, 0, 0), {ImmMode::kPcrelHi, target}), column);
    Emit(WithImm(std::move(second), {ImmMode::kPcrelLoPrev, target}), column);
  }

  Status Dispatch() {
    if (auto pseudo = DispatchPseudo(); pseudo.has_value()) {
      return *pseudo;
    }
    const std::optional<Opcode> opcode = LookupMnemonic(mnemonic_);
    if (!opcode) {
      return Fail(std::format("unknown instruction '{}'", mnemonic_));
    }
    return DispatchReal(*opcode);
  }

  Status DispatchReal(Opcode op) {
    switch (GetOpcodeInfo(op).format) {
      case InstFormat::kR:
        return RType(op);
      case InstFormat::kI:
        return IType(op, ImmSlot::kLow12);
      case InstFormat::kShiftImm:
        return IType(op, ImmSlot::kPlain);
      case InstFormat::kLoad:
        return Load(op);
      case InstFormat::kStore:
        return Store(op);
      case InstFormat::kBranch:
        return Branch(op, false);
      case InstFormat::kU:
        return UType(op);
      case InstFormat::kJal:
        return Jal();
      case InstFormat::kJalr:
        return Jalr();
      case InstFormat::kFence:
        return Fence();
      case InstFormat::kSystem:
        if (auto s = Count(0); !s) return s;
        Emit(Make(op, 0, 0, 0));
        return {};
      case InstFormat::kNone:
        break;
    }
    return Fail(std::format("unknown instruction '{}'", mnemonic_));
  }

  Status RType(Opcode op) {
    if (auto s = Count(3); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    auto rs1 = Reg(1);
    if (!rs1) return std::unexpected(rs1.error());
    if (ops_[2].kind != Operand::Kind::kRegister) {
      if (const auto imm_op = ImmediateForm(op)) {
        const ImmSlot slot =
            GetOpcodeInfo(*imm_op).format == InstFormat::kShiftImm
                ? ImmSlot::kPlain
                : ImmSlot::kLow12;
        auto imm = Imm(2, slot);
        if (!imm) return std::unexpected(imm.error());
        Emit(WithImm(Make(*imm_op, *rd, *rs1, 0), *imm));
        return {};
      }
    }
    auto rs2 = Reg(2);
    if (!rs2) return std::unexpected(rs2.error());
    Emit(Make(op, *rd, *rs1, *rs2));
    return {};
  }

  Status IType(Opcode op, ImmSlot slot) {
    if (auto s = Count(3); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    auto rs1 = Reg(1);
    if (!rs1) return std::unexpected(rs1.error());
    auto imm = Imm(2, slot);
    if (!imm) return std::unexpected(imm.error());
    Emit(WithImm(Make(op, *rd, *rs1, 0), *imm));
    return {};
  }

  Status Load(Opcode op) {
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    const Operand& addr = ops_[1];
    if (addr.kind == Operand::Kind::kMemory) {
      auto imm = ClassifyImm(addr.expr, ImmSlot::kLow12, addr.column);
      if (!imm) return std::unexpected(imm.error());
      Emit(WithImm(Make(op, *rd, addr.reg, 0), *imm));
      return {};
    }
    if (addr.kind == Operand::Kind::kExpression &&
        addr.expr->kind != Expr::Kind::kReloc) {
      // lw rd, symbol  ->  auipc rd, %pcrel_hi(symbol); lw rd, %pcrel_lo(rd)
      EmitPcrelPair(*rd, addr.expr, Make(op, *rd, *rd, 0), addr.column);
      return {};
    }
    return Fail("expected memory operand 'offset(reg)' or a symbol",
                addr.column);
  }

  Status Store(Opcode op) {
    if (ops_.size() == 3) {
      // sw rs2, symbol, rt  ->  auipc rt, %pcrel_hi; sw rs2, %pcrel_lo(rt)
      auto rs2 = Reg(0);
      if (!rs2) return std::unexpected(rs2.error());
      auto tmp = Reg(2);
      if (!tmp) return std::unexpected(tmp.error());
      if (ops_[1].kind != Operand::Kind::kExpression) {
        return Fail("expected a symbol", ops_[1].column);
      }
      EmitPcrelPair(*tmp, ops_[1].expr, Make(op, 0, *tmp, *rs2),
                    ops_[1].column);
      return {};
    }
    if (auto s = Count(2); !s) return s;
    auto rs2 = Reg(0);
    if (!rs2) return std::unexpected(rs2.error());
    const Operand& addr = ops_[1];
    if (addr.kind != Operand::Kind::kMemory) {
      return Fail(std::format("expected memory operand 'offset(reg)'; use "
                              "'{} rs2, symbol, temp' for symbols",
                              mnemonic_),
                  addr.column);
    }
    auto imm = ClassifyImm(addr.expr, ImmSlot::kLow12, addr.column);
    if (!imm) return std::unexpected(imm.error());
    Emit(WithImm(Make(op, 0, addr.reg, *rs2), *imm));
    return {};
  }

  // Branch with operands (rs1, rs2, target); `swap` exchanges rs1/rs2.
  Status Branch(Opcode op, bool swap) {
    if (auto s = Count(3); !s) return s;
    auto a = Reg(0);
    if (!a) return std::unexpected(a.error());
    auto b = Reg(1);
    if (!b) return std::unexpected(b.error());
    auto target = Imm(2, ImmSlot::kTarget);
    if (!target) return std::unexpected(target.error());
    Emit(WithImm(swap ? Make(op, 0, *b, *a) : Make(op, 0, *a, *b), *target));
    return {};
  }

  // Branch against zero: op rs, target. `zero_first` puts x0 in rs1.
  Status BranchZero(Opcode op, bool zero_first) {
    if (auto s = Count(2); !s) return s;
    auto rs = Reg(0);
    if (!rs) return std::unexpected(rs.error());
    auto target = Imm(1, ImmSlot::kTarget);
    if (!target) return std::unexpected(target.error());
    Emit(WithImm(zero_first ? Make(op, 0, reg::kZero, *rs)
                            : Make(op, 0, *rs, reg::kZero),
                 *target));
    return {};
  }

  Status UType(Opcode op) {
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    auto imm = Imm(1, op == kAuipc ? ImmSlot::kAuipcUpper : ImmSlot::kUpper);
    if (!imm) return std::unexpected(imm.error());
    Emit(WithImm(Make(op, *rd, 0, 0), *imm));
    return {};
  }

  Status Jal() {
    if (ops_.size() == 1) {
      auto target = Imm(0, ImmSlot::kTarget);
      if (!target) return std::unexpected(target.error());
      Emit(WithImm(Make(kJal, reg::kRa, 0, 0), *target));
      return {};
    }
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    auto target = Imm(1, ImmSlot::kTarget);
    if (!target) return std::unexpected(target.error());
    Emit(WithImm(Make(kJal, *rd, 0, 0), *target));
    return {};
  }

  Status Jalr() {
    switch (ops_.size()) {
      case 1: {
        const Operand& op = ops_[0];
        if (op.kind == Operand::Kind::kMemory) {
          auto imm = ClassifyImm(op.expr, ImmSlot::kLow12, op.column);
          if (!imm) return std::unexpected(imm.error());
          Emit(WithImm(Make(kJalr, reg::kRa, op.reg, 0), *imm));
          return {};
        }
        auto rs = Reg(0);
        if (!rs) return std::unexpected(rs.error());
        Emit(Make(kJalr, reg::kRa, *rs, 0));
        return {};
      }
      case 2: {
        auto rd = Reg(0);
        if (!rd) return std::unexpected(rd.error());
        const Operand& op = ops_[1];
        if (op.kind == Operand::Kind::kMemory) {
          auto imm = ClassifyImm(op.expr, ImmSlot::kLow12, op.column);
          if (!imm) return std::unexpected(imm.error());
          Emit(WithImm(Make(kJalr, *rd, op.reg, 0), *imm));
          return {};
        }
        auto rs = Reg(1);
        if (!rs) return std::unexpected(rs.error());
        Emit(Make(kJalr, *rd, *rs, 0));
        return {};
      }
      case 3:
        return IType(kJalr, ImmSlot::kLow12);
      default:
        return Fail(
            std::format("'jalr' expects 1 to 3 operands, got {}", ops_.size()));
    }
  }

  Status Fence() {
    if (ops_.empty()) {
      Emit(Make(kFence, 0, 0, 0, 0xFF));
      return {};
    }
    if (auto s = Count(2); !s) return s;
    int64_t sets[2] = {};
    for (size_t i = 0; i < 2; ++i) {
      const Operand& op = ops_[i];
      std::optional<int64_t> bits;
      if (op.kind == Operand::Kind::kExpression && op.expr &&
          op.expr->kind == Expr::Kind::kSymbol) {
        bits = ParseFenceSet(op.expr->name);
      } else if (op.kind == Operand::Kind::kExpression && op.expr &&
                 op.expr->kind == Expr::Kind::kNumber && op.expr->number == 0) {
        bits = 0;
      }
      if (!bits) {
        return Fail(
            "fence operands must be a combination of 'i', 'o', 'r', "
            "'w'",
            op.column);
      }
      sets[i] = *bits;
    }
    Emit(Make(kFence, 0, 0, 0, (sets[0] << 4) | sets[1]));
    return {};
  }

  // rd, rs -> op rd, a, b (with register layout given by the lambda).
  template <typename Fn>
  Status TwoReg(Fn&& build) {
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    auto rs = Reg(1);
    if (!rs) return std::unexpected(rs.error());
    Emit(build(*rd, *rs));
    return {};
  }

  Status LoadImmediate() {
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    const Operand& op = ops_[1];
    if (op.kind != Operand::Kind::kExpression || !op.expr ||
        op.expr->kind == Expr::Kind::kReloc) {
      return Fail("'li' expects a constant expression", op.column);
    }
    const std::optional<int64_t> value =
        ctx_.eval_constant ? ctx_.eval_constant(*op.expr) : std::nullopt;
    if (!value) {
      return Fail(
          "'li' requires a constant known at this point; use 'la' "
          "for addresses",
          op.column);
    }
    int64_t v = *value;
    if (ctx_.xlen == Xlen::k32) {
      if (v < std::numeric_limits<int32_t>::min() ||
          v > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return Fail(std::format("immediate {} does not fit in 32 bits", v),
                    op.column);
      }
      v = static_cast<int32_t>(static_cast<uint32_t>(v));
    }
    for (PendingInst& inst : ExpandLoadImmediate(*rd, v, ctx_.xlen)) {
      Emit(std::move(inst), op.column);
    }
    return {};
  }

  Status LoadAddress() {
    if (auto s = Count(2); !s) return s;
    auto rd = Reg(0);
    if (!rd) return std::unexpected(rd.error());
    const Operand& op = ops_[1];
    if (op.kind != Operand::Kind::kExpression ||
        op.expr->kind == Expr::Kind::kReloc) {
      return Fail("'la' expects a symbol expression", op.column);
    }
    EmitPcrelPair(*rd, op.expr, Make(kAddi, *rd, *rd, 0), op.column);
    return {};
  }

  Status CallOrTail(bool tail) {
    uint8_t link = tail ? reg::kZero : reg::kRa;
    uint8_t tmp = tail ? reg::kT1 : reg::kRa;
    size_t target_index = 0;
    if (ops_.size() == 2 && !tail) {
      auto rd = Reg(0);
      if (!rd) return std::unexpected(rd.error());
      link = tmp = *rd;
      target_index = 1;
    } else if (auto s = Count(1); !s) {
      return s;
    }
    const Operand& op = ops_[target_index];
    if (op.kind != Operand::Kind::kExpression ||
        op.expr->kind == Expr::Kind::kReloc) {
      return Fail(std::format("'{}' expects a symbol", mnemonic_), op.column);
    }
    EmitPcrelPair(tmp, op.expr, Make(kJalr, link, tmp, 0), op.column);
    return {};
  }

  Status JumpTo(uint8_t link) {
    if (auto s = Count(1); !s) return s;
    auto target = Imm(0, ImmSlot::kTarget);
    if (!target) return std::unexpected(target.error());
    Emit(WithImm(Make(kJal, link, 0, 0), *target));
    return {};
  }

  // Returns std::nullopt when `mnemonic_` is not a pseudo-instruction.
  std::optional<Status> DispatchPseudo() {
    const std::string_view m = mnemonic_;
    using R = PendingInst;
    if (m == "nop") {
      if (auto s = Count(0); !s) return s;
      Emit(Make(kAddi, 0, 0, 0));
      return Status{};
    }
    if (m == "li") return LoadImmediate();
    if (m == "la" || m == "lla") return LoadAddress();
    if (m == "mv") {
      return TwoReg(
          [](uint8_t d, uint8_t s) -> R { return Make(kAddi, d, s, 0); });
    }
    if (m == "not") {
      return TwoReg(
          [](uint8_t d, uint8_t s) -> R { return Make(kXori, d, s, 0, -1); });
    }
    if (m == "neg") {
      return TwoReg([](uint8_t d, uint8_t s) -> R {
        return Make(kSub, d, reg::kZero, s);
      });
    }
    if (m == "negw") {
      return TwoReg([](uint8_t d, uint8_t s) -> R {
        return Make(kSubw, d, reg::kZero, s);
      });
    }
    if (m == "sext.w") {
      return TwoReg(
          [](uint8_t d, uint8_t s) -> R { return Make(kAddiw, d, s, 0); });
    }
    if (m == "zext.b") {
      return TwoReg(
          [](uint8_t d, uint8_t s) -> R { return Make(kAndi, d, s, 0, 255); });
    }
    if (m == "seqz") {
      return TwoReg(
          [](uint8_t d, uint8_t s) -> R { return Make(kSltiu, d, s, 0, 1); });
    }
    if (m == "snez") {
      return TwoReg([](uint8_t d, uint8_t s) -> R {
        return Make(kSltu, d, reg::kZero, s);
      });
    }
    if (m == "sltz") {
      return TwoReg([](uint8_t d, uint8_t s) -> R {
        return Make(kSlt, d, s, reg::kZero);
      });
    }
    if (m == "sgtz") {
      return TwoReg([](uint8_t d, uint8_t s) -> R {
        return Make(kSlt, d, reg::kZero, s);
      });
    }
    if (m == "beqz") return BranchZero(kBeq, false);
    if (m == "bnez") return BranchZero(kBne, false);
    if (m == "blez") return BranchZero(kBge, true);
    if (m == "bgez") return BranchZero(kBge, false);
    if (m == "bltz") return BranchZero(kBlt, false);
    if (m == "bgtz") return BranchZero(kBlt, true);
    if (m == "bgt") return Branch(kBlt, true);
    if (m == "ble") return Branch(kBge, true);
    if (m == "bgtu") return Branch(kBltu, true);
    if (m == "bleu") return Branch(kBgeu, true);
    if (m == "j") return JumpTo(reg::kZero);
    if (m == "jr") {
      if (auto s = Count(1); !s) return s;
      auto rs = Reg(0);
      if (!rs) return std::unexpected(rs.error());
      Emit(Make(kJalr, reg::kZero, *rs, 0));
      return Status{};
    }
    if (m == "ret") {
      if (auto s = Count(0); !s) return s;
      Emit(Make(kJalr, reg::kZero, reg::kRa, 0));
      return Status{};
    }
    if (m == "call") return CallOrTail(false);
    if (m == "tail") return CallOrTail(true);
    if (m == "fence.tso") {
      if (auto s = Count(0); !s) return s;
      Emit(Make(kFence, 0, 0, 0, 0x833));
      return Status{};
    }
    return std::nullopt;
  }

  std::string_view mnemonic_;
  std::span<const Operand> ops_;
  const ExpandContext& ctx_;
  std::vector<PendingInst> out_;
};

void LoadConst(std::vector<PendingInst>& out, uint8_t rd, int64_t value,
               Xlen xlen, bool top_level) {
  const bool rv64 = xlen == Xlen::k64;
  const int64_t lower = SignExtend12(value);
  const int64_t upper = static_cast<int64_t>(static_cast<uint64_t>(value) -
                                             static_cast<uint64_t>(lower));
  const bool fits32 = value >= std::numeric_limits<int32_t>::min() &&
                      value <= std::numeric_limits<int32_t>::max();
  if (rv64 && !fits32) {
    // Reduce to a 32-bit signed constant using SLLI and ADDI (GNU as).
    int shift = 12;
    while (((static_cast<uint64_t>(upper) >> shift) & 1) == 0) {
      ++shift;
    }
    LoadConst(out, rd, upper >> shift, xlen, false);
    out.push_back(Make(kSlli, rd, rd, 0, shift));
    if (lower != 0) {
      out.push_back(Make(kAddi, rd, rd, 0, lower));
    }
    return;
  }
  uint8_t hi_reg = reg::kZero;
  if (upper != 0) {
    const auto upper20 = static_cast<int64_t>(
        static_cast<int32_t>(static_cast<uint32_t>(upper) & 0xFFFFF000u));
    out.push_back(Make(kLui, rd, 0, 0, upper20));
    hi_reg = rd;
  }
  if (lower != 0 || hi_reg == reg::kZero) {
    const bool use_addiw = rv64 && !(top_level && hi_reg == reg::kZero);
    out.push_back(Make(use_addiw ? kAddiw : kAddi, rd, hi_reg, 0, lower));
  }
}

}  // namespace

std::vector<PendingInst> ExpandLoadImmediate(uint8_t rd, int64_t value,
                                             Xlen xlen) {
  std::vector<PendingInst> out;
  LoadConst(out, rd, value, xlen, true);
  return out;
}

std::expected<std::vector<PendingInst>, AsmError> ExpandInstruction(
    std::string_view mnemonic, std::span<const Operand> operands,
    const ExpandContext& context) {
  return Expander(mnemonic, operands, context).Run();
}

}  // namespace rvsim
