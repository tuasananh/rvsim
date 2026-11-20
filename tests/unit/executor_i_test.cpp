#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "simulator/core/cpu_state.h"
#include "simulator/core/decoder.h"
#include "simulator/core/encoder.h"
#include "simulator/core/executor.h"
#include "simulator/core/registers.h"
#include "simulator/memory/memory_bus.h"

namespace rvsim {
namespace {

using namespace reg;  // NOLINT(build/namespaces)

class ExecutorTest : public ::testing::Test {
 protected:
  void SetXlen(Xlen xlen) { state_.xlen = xlen; }

  ExecResult Exec(Opcode op, uint8_t rd, uint8_t rs1, uint8_t rs2,
                  int64_t imm) {
    auto raw = Encode(op, rd, rs1, rs2, imm, state_.xlen);
    EXPECT_TRUE(raw) << raw.error();
    const DecodedInst inst = Decode(raw.value_or(0), state_.xlen);
    EXPECT_TRUE(inst.valid());
    return Execute(state_, bus_, inst);
  }

  void Set(uint8_t r, uint64_t value) { state_.WriteReg(r, value); }
  uint64_t Get(uint8_t r) const { return state_.ReadReg(r); }

  CpuState state_;
  MemoryBus bus_;
};

TEST_F(ExecutorTest, Rv32AddWrapsAround) {
  Set(kA0, 0xFFFFFFFF);
  Set(kA1, 1);
  ASSERT_EQ(Exec(Opcode::kAdd, kA2, kA0, kA1, 0).status, ExecStatus::kOk);
  EXPECT_EQ(Get(kA2), 0u);
  EXPECT_EQ(state_.pc, 4u);
}

TEST_F(ExecutorTest, Rv32SubAtInt32Min) {
  Set(kA0, 0x80000000);
  Set(kA1, 1);
  Exec(Opcode::kSub, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0x7FFFFFFFu);
}

TEST_F(ExecutorTest, Rv32RegistersStayMaskedTo32Bits) {
  Exec(Opcode::kAddi, kA0, kZero, 0, -1);
  EXPECT_EQ(Get(kA0), 0xFFFFFFFFu);
  EXPECT_EQ(state_.ReadSigned(kA0), -1);
}

TEST_F(ExecutorTest, Rv64AddwSignExtends) {
  SetXlen(Xlen::k64);
  Set(kA0, 0x7FFFFFFF);
  Set(kA1, 1);
  Exec(Opcode::kAddw, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFF80000000ULL);
  Exec(Opcode::kAddiw, kA2, kA0, 0, 1);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFF80000000ULL);
}

TEST_F(ExecutorTest, Rv64AddwIgnoresUpperBits) {
  SetXlen(Xlen::k64);
  Set(kA0, 0x1234567800000005ULL);
  Exec(Opcode::kAddiw, kA1, kA0, 0, 0);  // sext.w
  EXPECT_EQ(Get(kA1), 5u);
}

TEST_F(ExecutorTest, Rv64SubAtInt64Min) {
  SetXlen(Xlen::k64);
  Set(kA0, 0x8000000000000000ULL);
  Set(kA1, 1);
  Exec(Opcode::kSub, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0x7FFFFFFFFFFFFFFFULL);
  Exec(Opcode::kSlt, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 1u);
  Exec(Opcode::kSltu, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0u);
}

TEST_F(ExecutorTest, SignedVersusUnsignedCompares) {
  Set(kA0, 0xFFFFFFFF);  // -1
  Set(kA1, 1);
  Exec(Opcode::kSlt, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 1u);
  Exec(Opcode::kSltu, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0u);
  Exec(Opcode::kSlti, kA2, kA1, 0, -1);
  EXPECT_EQ(Get(kA2), 0u);
  // sltiu compares against the sign-extended immediate as unsigned.
  Exec(Opcode::kSltiu, kA2, kA1, 0, -1);
  EXPECT_EQ(Get(kA2), 1u);
}

TEST_F(ExecutorTest, Rv32ShiftAmountsUseLowFiveBits) {
  Set(kA0, 1);
  Set(kA1, 33);
  Exec(Opcode::kSll, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 2u);
  Set(kA0, 0x80000000);
  Exec(Opcode::kSra, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0xC0000000u);
  Exec(Opcode::kSrl, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 0x40000000u);
  Exec(Opcode::kSrai, kA2, kA0, 0, 31);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFFu);
}

TEST_F(ExecutorTest, Rv64ShiftAmountsUseLowSixBits) {
  SetXlen(Xlen::k64);
  Set(kA0, 1);
  Set(kA1, 65);
  Exec(Opcode::kSll, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 2u);
  Exec(Opcode::kSlli, kA2, kA0, 0, 63);
  EXPECT_EQ(Get(kA2), 0x8000000000000000ULL);
  Set(kA0, 0x8000000000000000ULL);
  Exec(Opcode::kSrai, kA2, kA0, 0, 63);
  EXPECT_EQ(Get(kA2), ~0ULL);
  Exec(Opcode::kSrli, kA2, kA0, 0, 63);
  EXPECT_EQ(Get(kA2), 1u);
}

TEST_F(ExecutorTest, Rv64WordShiftsMaskAndSignExtend) {
  SetXlen(Xlen::k64);
  Set(kA0, 1);
  Set(kA1, 33);  // masked to 1
  Exec(Opcode::kSllw, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 2u);
  Exec(Opcode::kSlliw, kA2, kA0, 0, 31);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFF80000000ULL);
  Set(kA0, 0xFFFFFFFF80000000ULL);
  Exec(Opcode::kSrliw, kA2, kA0, 0, 4);
  EXPECT_EQ(Get(kA2), 0x08000000u);
  Exec(Opcode::kSraiw, kA2, kA0, 0, 4);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFFF8000000ULL);
  Set(kA1, 31);
  Exec(Opcode::kSraw, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), ~0ULL);
  Exec(Opcode::kSrlw, kA2, kA0, kA1, 0);
  EXPECT_EQ(Get(kA2), 1u);
}

TEST_F(ExecutorTest, X0IsImmutable) {
  Exec(Opcode::kAddi, kZero, kZero, 0, 5);
  EXPECT_EQ(Get(kZero), 0u);
  Exec(Opcode::kLui, kZero, 0, 0, 0x12345000);
  EXPECT_EQ(Get(kZero), 0u);
}

TEST_F(ExecutorTest, LuiAndAuipcSignExtendOnRv64) {
  SetXlen(Xlen::k64);
  Exec(Opcode::kLui, kA0, 0, 0, -2147483648LL);
  EXPECT_EQ(Get(kA0), 0xFFFFFFFF80000000ULL);
  state_.pc = 0x1000;
  Exec(Opcode::kAuipc, kA1, 0, 0, 0x2000);
  EXPECT_EQ(Get(kA1), 0x3000u);
  EXPECT_EQ(state_.pc, 0x1004u);
}

TEST_F(ExecutorTest, LuiOnRv32) {
  Exec(Opcode::kLui, kA0, 0, 0, -2147483648LL);
  EXPECT_EQ(Get(kA0), 0x80000000u);
}

TEST_F(ExecutorTest, JalLinksAndJumpsBackwards) {
  state_.pc = 0x400100;
  Exec(Opcode::kJal, kRa, 0, 0, -0x100);
  EXPECT_EQ(Get(kRa), 0x400104u);
  EXPECT_EQ(state_.pc, 0x400000u);
}

TEST_F(ExecutorTest, JalrClearsBitZeroAndHandlesRdEqualsRs1) {
  state_.pc = 0x400000;
  Set(kA0, 0x400201);
  Exec(Opcode::kJalr, kA0, kA0, 0, 3);  // (0x400201 + 3) & ~1 = 0x400204
  EXPECT_EQ(state_.pc, 0x400204u);
  EXPECT_EQ(Get(kA0), 0x400004u);
}

TEST_F(ExecutorTest, MisalignedJumpTargetTrapsWithoutSideEffects) {
  state_.pc = 0x400000;
  Set(kA0, 0x400002);
  const ExecResult result = Exec(Opcode::kJalr, kRa, kA0, 0, 0);
  EXPECT_EQ(result.status, ExecStatus::kTrap);
  EXPECT_EQ(result.trap,
            (Trap{TrapCause::kInstructionAddressMisaligned, 0x400002}));
  EXPECT_EQ(state_.pc, 0x400000u);
  EXPECT_EQ(Get(kRa), 0u);
  const ExecResult jal = Exec(Opcode::kJal, kRa, 0, 0, 6);
  EXPECT_EQ(jal.status, ExecStatus::kTrap);
}

TEST_F(ExecutorTest, BranchesTakenAndNotTaken) {
  state_.pc = 0x1000;
  Set(kA0, 0xFFFFFFFF);
  Set(kA1, 1);
  Exec(Opcode::kBlt, 0, kA0, kA1, -16);
  EXPECT_EQ(state_.pc, 0x0FF0u);
  Exec(Opcode::kBltu, 0, kA0, kA1, -16);
  EXPECT_EQ(state_.pc, 0x0FF4u);
  Exec(Opcode::kBgeu, 0, kA0, kA1, 8);
  EXPECT_EQ(state_.pc, 0x0FFCu);
  Exec(Opcode::kBge, 0, kA0, kA1, 8);
  EXPECT_EQ(state_.pc, 0x1000u);
  Exec(Opcode::kBeq, 0, kA0, kA0, 4094 - 2);
  EXPECT_EQ(state_.pc, 0x1000u + 4092);
  Exec(Opcode::kBne, 0, kA0, kA0, 8);
  EXPECT_EQ(state_.pc, 0x1000u + 4096);
}

TEST_F(ExecutorTest, LoadsSignAndZeroExtend) {
  ASSERT_TRUE(bus_.Write32(0x2000, 0x8081F0FF));
  Set(kA0, 0x2000);
  Exec(Opcode::kLb, kA1, kA0, 0, 0);
  EXPECT_EQ(Get(kA1), 0xFFFFFFFFu);
  Exec(Opcode::kLbu, kA1, kA0, 0, 0);
  EXPECT_EQ(Get(kA1), 0xFFu);
  Exec(Opcode::kLh, kA1, kA0, 0, 2);
  EXPECT_EQ(Get(kA1), 0xFFFF8081u);
  Exec(Opcode::kLhu, kA1, kA0, 0, 2);
  EXPECT_EQ(Get(kA1), 0x8081u);
  Exec(Opcode::kLw, kA1, kA0, 0, 0);
  EXPECT_EQ(Get(kA1), 0x8081F0FFu);
}

TEST_F(ExecutorTest, Rv64LoadsAndStores) {
  SetXlen(Xlen::k64);
  Set(kA0, 0x10000000);
  Set(kA1, 0x8877665544332211ULL);
  Exec(Opcode::kSd, 0, kA0, kA1, 8);
  EXPECT_EQ(*bus_.Read64(0x10000008), 0x8877665544332211ULL);
  Exec(Opcode::kLd, kA2, kA0, 0, 8);
  EXPECT_EQ(Get(kA2), 0x8877665544332211ULL);
  Exec(Opcode::kLw, kA2, kA0, 0, 12);
  EXPECT_EQ(Get(kA2), 0xFFFFFFFF88776655ULL);
  Exec(Opcode::kLwu, kA2, kA0, 0, 12);
  EXPECT_EQ(Get(kA2), 0x88776655ULL);
}

TEST_F(ExecutorTest, StoresWriteLowBytesOnly) {
  Set(kA0, 0x3000);
  Set(kA1, 0x12345678);
  Exec(Opcode::kSb, 0, kA0, kA1, -1);
  Exec(Opcode::kSh, 0, kA0, kA1, 2);
  EXPECT_EQ(*bus_.Read8(0x2FFF), 0x78);
  EXPECT_EQ(*bus_.Read32(0x3000), 0x56780000u);
}

TEST_F(ExecutorTest, Rv32EffectiveAddressWrapsAround) {
  Set(kA0, 0xFFFFFFFC);
  Set(kA1, 0xABCD);
  Exec(Opcode::kSw, 0, kA0, kA1, 8);
  EXPECT_EQ(*bus_.Read32(0x4), 0xABCDu);
}

TEST_F(ExecutorTest, StrictAlignmentLoadTrapLeavesStateUnchanged) {
  bus_.set_strict_alignment(true);
  state_.pc = 0x100;
  Set(kA0, 0x2001);
  const ExecResult result = Exec(Opcode::kLw, kA1, kA0, 0, 0);
  EXPECT_EQ(result.status, ExecStatus::kTrap);
  EXPECT_EQ(result.trap, (Trap{TrapCause::kLoadAddressMisaligned, 0x2001}));
  EXPECT_EQ(state_.pc, 0x100u);
}

TEST_F(ExecutorTest, EcallAndEbreakDoNotAdvancePc) {
  state_.pc = 0x40;
  EXPECT_EQ(Exec(Opcode::kEcall, 0, 0, 0, 0).status, ExecStatus::kEcall);
  EXPECT_EQ(Exec(Opcode::kEbreak, 0, 0, 0, 0).status, ExecStatus::kEbreak);
  EXPECT_EQ(state_.pc, 0x40u);
}

TEST_F(ExecutorTest, FenceIsNop) {
  Exec(Opcode::kFence, 0, 0, 0, 0xFF);
  EXPECT_EQ(state_.pc, 4u);
}

TEST_F(ExecutorTest, IllegalInstructionTraps) {
  const ExecResult result =
      Execute(state_, bus_, Decode(0xFFFFFFFF, state_.xlen));
  EXPECT_EQ(result.status, ExecStatus::kTrap);
  EXPECT_EQ(result.trap, (Trap{TrapCause::kIllegalInstruction, 0xFFFFFFFF}));
}

}  // namespace
}  // namespace rvsim
