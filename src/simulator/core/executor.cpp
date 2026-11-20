#include "simulator/core/executor.h"

#include <type_traits>

namespace rvsim {
namespace {

template <Xlen kXlen>
struct XlenTypes {
  using U = std::conditional_t<kXlen == Xlen::k32, uint32_t, uint64_t>;
  using S = std::make_signed_t<U>;
  static constexpr unsigned kShiftMask = kXlen == Xlen::k32 ? 0x1F : 0x3F;
};

// Sign-extends a 32-bit result to 64 bits (for RV64 *W instructions).
constexpr uint64_t SextW(uint32_t value) {
  return static_cast<uint64_t>(
      static_cast<int64_t>(static_cast<int32_t>(value)));
}

constexpr ExecResult TrapResult(TrapCause cause, uint64_t tval) {
  return ExecResult{ExecStatus::kTrap, Trap{cause, tval}};
}

}  // namespace

template <Xlen kXlen>
ExecResult Execute(CpuState& state, MemoryBus& bus, const DecodedInst& inst) {
  using U = typename XlenTypes<kXlen>::U;
  using S = typename XlenTypes<kXlen>::S;
  constexpr unsigned kShiftMask = XlenTypes<kXlen>::kShiftMask;

  const U pc = static_cast<U>(state.pc);
  const U a = static_cast<U>(state.x[inst.rs1]);
  const U b = static_cast<U>(state.x[inst.rs2]);
  const U imm = static_cast<U>(inst.imm);
  const U next_pc = static_cast<U>(pc + inst.length);
  const auto write = [&](U value) {
    state.WriteReg(inst.rd, static_cast<uint64_t>(value));
  };

  // Control transfer helper enforcing IALIGN=32 (no C extension yet).
  const auto jump = [&](U target) -> ExecResult {
    if ((target & 0x3) != 0) {
      return TrapResult(TrapCause::kInstructionAddressMisaligned, target);
    }
    state.pc = target;
    return {};
  };

  const auto load = [&]<typename T, typename Ext>() -> ExecResult {
    const U addr = static_cast<U>(a + imm);
    auto value = bus.Load<T>(addr);
    if (!value) {
      return ExecResult{ExecStatus::kTrap, value.error()};
    }
    write(static_cast<U>(static_cast<Ext>(*value)));
    state.pc = next_pc;
    return {};
  };

  const auto store = [&]<typename T>() -> ExecResult {
    const U addr = static_cast<U>(a + imm);
    auto result = bus.Store<T>(addr, static_cast<T>(b));
    if (!result) {
      return ExecResult{ExecStatus::kTrap, result.error()};
    }
    state.pc = next_pc;
    return {};
  };

  switch (inst.op) {
    case Opcode::kLui:
      write(imm);
      break;
    case Opcode::kAuipc:
      write(static_cast<U>(pc + imm));
      break;
    case Opcode::kJal: {
      const U target = static_cast<U>(pc + imm);
      if ((target & 0x3) != 0) {
        return TrapResult(TrapCause::kInstructionAddressMisaligned, target);
      }
      write(next_pc);
      state.pc = target;
      return {};
    }
    case Opcode::kJalr: {
      const U target = static_cast<U>((a + imm) & ~U{1});
      if ((target & 0x3) != 0) {
        return TrapResult(TrapCause::kInstructionAddressMisaligned, target);
      }
      write(next_pc);
      state.pc = target;
      return {};
    }
    case Opcode::kBeq:
      return a == b ? jump(static_cast<U>(pc + imm)) : jump(next_pc);
    case Opcode::kBne:
      return a != b ? jump(static_cast<U>(pc + imm)) : jump(next_pc);
    case Opcode::kBlt:
      return static_cast<S>(a) < static_cast<S>(b)
                 ? jump(static_cast<U>(pc + imm))
                 : jump(next_pc);
    case Opcode::kBge:
      return static_cast<S>(a) >= static_cast<S>(b)
                 ? jump(static_cast<U>(pc + imm))
                 : jump(next_pc);
    case Opcode::kBltu:
      return a < b ? jump(static_cast<U>(pc + imm)) : jump(next_pc);
    case Opcode::kBgeu:
      return a >= b ? jump(static_cast<U>(pc + imm)) : jump(next_pc);

    case Opcode::kLb:
      return load.template operator()<uint8_t, int8_t>();
    case Opcode::kLh:
      return load.template operator()<uint16_t, int16_t>();
    case Opcode::kLw:
      return load.template operator()<uint32_t, int32_t>();
    case Opcode::kLbu:
      return load.template operator()<uint8_t, uint8_t>();
    case Opcode::kLhu:
      return load.template operator()<uint16_t, uint16_t>();
    case Opcode::kLwu:
      return load.template operator()<uint32_t, uint32_t>();
    case Opcode::kLd:
      return load.template operator()<uint64_t, uint64_t>();
    case Opcode::kSb:
      return store.template operator()<uint8_t>();
    case Opcode::kSh:
      return store.template operator()<uint16_t>();
    case Opcode::kSw:
      return store.template operator()<uint32_t>();
    case Opcode::kSd:
      return store.template operator()<uint64_t>();

    case Opcode::kAddi:
      write(static_cast<U>(a + imm));
      break;
    case Opcode::kSlti:
      write(static_cast<S>(a) < static_cast<S>(imm) ? 1 : 0);
      break;
    case Opcode::kSltiu:
      write(a < imm ? 1 : 0);
      break;
    case Opcode::kXori:
      write(a ^ imm);
      break;
    case Opcode::kOri:
      write(a | imm);
      break;
    case Opcode::kAndi:
      write(a & imm);
      break;
    case Opcode::kSlli:
      write(static_cast<U>(a << (imm & kShiftMask)));
      break;
    case Opcode::kSrli:
      write(static_cast<U>(a >> (imm & kShiftMask)));
      break;
    case Opcode::kSrai:
      write(static_cast<U>(static_cast<S>(a) >> (imm & kShiftMask)));
      break;

    case Opcode::kAdd:
      write(static_cast<U>(a + b));
      break;
    case Opcode::kSub:
      write(static_cast<U>(a - b));
      break;
    case Opcode::kSll:
      write(static_cast<U>(a << (b & kShiftMask)));
      break;
    case Opcode::kSlt:
      write(static_cast<S>(a) < static_cast<S>(b) ? 1 : 0);
      break;
    case Opcode::kSltu:
      write(a < b ? 1 : 0);
      break;
    case Opcode::kXor:
      write(a ^ b);
      break;
    case Opcode::kSrl:
      write(static_cast<U>(a >> (b & kShiftMask)));
      break;
    case Opcode::kSra:
      write(static_cast<U>(static_cast<S>(a) >> (b & kShiftMask)));
      break;
    case Opcode::kOr:
      write(a | b);
      break;
    case Opcode::kAnd:
      write(a & b);
      break;

    case Opcode::kAddiw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a + imm))));
      break;
    case Opcode::kSlliw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a) << (imm & 0x1F))));
      break;
    case Opcode::kSrliw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a) >> (imm & 0x1F))));
      break;
    case Opcode::kSraiw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(
          static_cast<int32_t>(static_cast<uint32_t>(a)) >> (imm & 0x1F)))));
      break;
    case Opcode::kAddw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a + b))));
      break;
    case Opcode::kSubw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a - b))));
      break;
    case Opcode::kSllw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a) << (b & 0x1F))));
      break;
    case Opcode::kSrlw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(a) >> (b & 0x1F))));
      break;
    case Opcode::kSraw:
      write(static_cast<U>(SextW(static_cast<uint32_t>(
          static_cast<int32_t>(static_cast<uint32_t>(a)) >> (b & 0x1F)))));
      break;

    case Opcode::kFence:
      break;  // Single hart, sequentially consistent memory: no-op.
    case Opcode::kEcall:
      return ExecResult{ExecStatus::kEcall, {}};
    case Opcode::kEbreak:
      return ExecResult{ExecStatus::kEbreak, {}};

    case Opcode::kInvalid:
    case Opcode::kCount:
      return TrapResult(TrapCause::kIllegalInstruction, inst.raw);
  }

  state.pc = next_pc;
  return {};
}

template ExecResult Execute<Xlen::k32>(CpuState&, MemoryBus&,
                                       const DecodedInst&);
template ExecResult Execute<Xlen::k64>(CpuState&, MemoryBus&,
                                       const DecodedInst&);

ExecResult Execute(CpuState& state, MemoryBus& bus, const DecodedInst& inst) {
  return state.xlen == Xlen::k32 ? Execute<Xlen::k32>(state, bus, inst)
                                 : Execute<Xlen::k64>(state, bus, inst);
}

}  // namespace rvsim
