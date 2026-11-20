#include "simulator/env/rars_env.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "simulator/core/encoder.h"
#include "simulator/core/registers.h"
#include "simulator/env/buffered_console.h"
#include "simulator/runtime/machine.h"

namespace rvsim {
namespace {

using namespace reg;  // NOLINT(build/namespaces)

constexpr uint64_t kTextBase = 0x00400000;
constexpr uint64_t kDataBase = 0x10000000;

struct Inst {
  Opcode op;
  uint8_t rd = 0, rs1 = 0, rs2 = 0;
  int64_t imm = 0;
};

ProgramImage MakeImage(Xlen xlen, std::initializer_list<Inst> code,
                       std::string data = {}) {
  ProgramImage image;
  image.xlen = xlen;
  image.entry_point = kTextBase;
  image.initial_sp = 0x7FFFFFF0;
  image.initial_gp = kDataBase + 0x800;
  Segment text{.name = ".text", .address = kTextBase, .executable = true};
  for (const Inst& inst : code) {
    auto raw = Encode(inst.op, inst.rd, inst.rs1, inst.rs2, inst.imm, xlen);
    EXPECT_TRUE(raw) << raw.error();
    const uint32_t word = raw.value_or(0);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&word);
    text.data.insert(text.data.end(), bytes, bytes + 4);
  }
  text.mem_size = text.data.size();
  image.segments.push_back(std::move(text));
  Segment data_seg{.name = ".data", .address = kDataBase, .writable = true};
  data_seg.data.assign(data.begin(), data.end());
  data_seg.mem_size = data_seg.data.size() + 64;
  image.heap_start = kDataBase + 0x1000;
  image.segments.push_back(std::move(data_seg));
  return image;
}

class RarsTest : public ::testing::Test {
 protected:
  void Load(Xlen xlen, std::initializer_list<Inst> code,
            std::string data = {}) {
    machine_.Configure(MachineConfig{.xlen = xlen});
    auto loaded = machine_.Load(MakeImage(xlen, code, std::move(data)));
    ASSERT_TRUE(loaded) << loaded.error().message;
  }

  BufferedConsole console_;
  Machine machine_{MachineConfig{}, console_};
};

TEST_F(RarsTest, PrintIntSignedAndExit) {
  Load(Xlen::k32, {
                      {Opcode::kAddi, kA0, kZero, 0, -42},
                      {Opcode::kAddi, kA7, kZero, 0, 1},
                      {Opcode::kEcall},
                      {Opcode::kAddi, kA7, kZero, 0, 10},
                      {Opcode::kEcall},
                  });
  const RunResult result = machine_.Run(100);
  EXPECT_EQ(result.status, StepStatus::kExited);
  EXPECT_EQ(console_.stdout_text(), "-42");
  EXPECT_EQ(machine_.run_state(), RunState::kExited);
  EXPECT_EQ(machine_.exit_code(), 0);
  EXPECT_EQ(machine_.state().instret, 5u);
}

TEST_F(RarsTest, Exit2ReturnsCode) {
  Load(Xlen::k64, {
                      {Opcode::kAddi, kA0, kZero, 0, 7},
                      {Opcode::kAddi, kA7, kZero, 0, 93},
                      {Opcode::kEcall},
                  });
  machine_.Run(100);
  EXPECT_EQ(machine_.run_state(), RunState::kExited);
  EXPECT_EQ(machine_.exit_code(), 7);
}

TEST_F(RarsTest, PrintStringHexBinUnsignedChar) {
  Load(Xlen::k32,
       {
           {Opcode::kLui, kA0, 0, 0, static_cast<int64_t>(kDataBase)},
           {Opcode::kAddi, kA7, kZero, 0, 4},
           {Opcode::kEcall},
           {Opcode::kAddi, kA0, kZero, 0, -1},
           {Opcode::kAddi, kA7, kZero, 0, 34},
           {Opcode::kEcall},
           {Opcode::kAddi, kA7, kZero, 0, 36},
           {Opcode::kEcall},
           {Opcode::kAddi, kA0, kZero, 0, 5},
           {Opcode::kAddi, kA7, kZero, 0, 35},
           {Opcode::kEcall},
           {Opcode::kAddi, kA0, kZero, 0, '!'},
           {Opcode::kAddi, kA7, kZero, 0, 11},
           {Opcode::kEcall},
           {Opcode::kAddi, kA7, kZero, 0, 10},
           {Opcode::kEcall},
       },
       std::string("Hi\n\0junk", 8));
  machine_.Run(100);
  EXPECT_EQ(console_.stdout_text(),
            "Hi\n0xffffffff4294967295"
            "00000000000000000000000000000101!");
}

TEST_F(RarsTest, Rv64PrintHexUses16Digits) {
  Load(Xlen::k64, {
                      {Opcode::kAddi, kA0, kZero, 0, -1},
                      {Opcode::kAddi, kA7, kZero, 0, 34},
                      {Opcode::kEcall},
                      {Opcode::kAddi, kA7, kZero, 0, 1},
                      {Opcode::kEcall},
                  });
  machine_.Run(5);
  EXPECT_EQ(console_.stdout_text(), "0xffffffffffffffff-1");
}

TEST_F(RarsTest, ReadIntWaitsForInputThenResumes) {
  Load(Xlen::k32, {
                      {Opcode::kAddi, kA7, kZero, 0, 5},
                      {Opcode::kEcall},
                      {Opcode::kAddi, kA7, kZero, 0, 1},
                      {Opcode::kEcall},
                  });
  RunResult result = machine_.Run(100);
  EXPECT_EQ(result.status, StepStatus::kWaitingForInput);
  EXPECT_EQ(machine_.state().pc, kTextBase + 4);
  EXPECT_EQ(machine_.run_state(), RunState::kReady);

  console_.PushInput("  -123\n");
  result = machine_.Run(3);
  EXPECT_EQ(console_.stdout_text(), "-123");
  EXPECT_EQ(machine_.state().ReadSigned(kA0), -123);
}

TEST_F(RarsTest, ReadStringBehavesLikeFgets) {
  Load(Xlen::k32,
       {
           {Opcode::kLui, kA0, 0, 0, static_cast<int64_t>(kDataBase)},
           {Opcode::kAddi, kA1, kZero, 0, 5},
           {Opcode::kAddi, kA7, kZero, 0, 8},
           {Opcode::kEcall},
       });
  console_.PushInput("hello world\n");
  machine_.Run(4);
  char buffer[8] = {};
  machine_.bus().ram().ReadRegion(
      kDataBase, std::span(reinterpret_cast<uint8_t*>(buffer), 8));
  EXPECT_STREQ(buffer, "hell");
}

TEST_F(RarsTest, ReadCharReturnsMinusOneOnEof) {
  Load(Xlen::k32, {
                      {Opcode::kAddi, kA7, kZero, 0, 12},
                      {Opcode::kEcall},
                      {Opcode::kEcall},
                  });
  console_.PushInput("A");
  console_.CloseInput();
  machine_.Run(2);
  EXPECT_EQ(machine_.state().ReadSigned(kA0), 'A');
  machine_.Run(1);
  EXPECT_EQ(machine_.state().ReadSigned(kA0), -1);
}

TEST_F(RarsTest, SbrkReturnsAlignedOldBreak) {
  Load(Xlen::k32, {
                      {Opcode::kAddi, kA0, kZero, 0, 13},
                      {Opcode::kAddi, kA7, kZero, 0, 9},
                      {Opcode::kEcall},
                      {Opcode::kAddi, kS0 + 1, kA0, 0, 0},
                      {Opcode::kAddi, kA0, kZero, 0, 4},
                      {Opcode::kEcall},
                  });
  machine_.Run(6);
  EXPECT_EQ(machine_.state().ReadReg(kS0 + 1), kDataBase + 0x1000);
  EXPECT_EQ(machine_.state().ReadReg(kA0), kDataBase + 0x1000 + 16);
}

TEST_F(RarsTest, UnsupportedServiceFaults) {
  Load(Xlen::k32, {
                      {Opcode::kAddi, kA7, kZero, 0, 999},
                      {Opcode::kEcall},
                  });
  const RunResult result = machine_.Run(10);
  EXPECT_EQ(result.status, StepStatus::kFaulted);
  EXPECT_EQ(machine_.run_state(), RunState::kFaulted);
  EXPECT_NE(machine_.fault_message().find("a7=999"), std::string::npos);
}

TEST(MachineTest, InitialRegistersFollowImage) {
  BufferedConsole console;
  Machine machine(MachineConfig{.xlen = Xlen::k64}, console);
  ASSERT_TRUE(machine.Load(MakeImage(Xlen::k64, {{Opcode::kEbreak}})));
  EXPECT_EQ(machine.state().pc, kTextBase);
  EXPECT_EQ(machine.state().ReadReg(kSp), 0x7FFFFFF0u);
  EXPECT_EQ(machine.state().ReadReg(kGp), kDataBase + 0x800);
}

TEST(MachineTest, RejectsXlenMismatch) {
  BufferedConsole console;
  Machine machine(MachineConfig{.xlen = Xlen::k32}, console);
  EXPECT_FALSE(machine.Load(MakeImage(Xlen::k64, {{Opcode::kEbreak}})));
  EXPECT_EQ(machine.run_state(), RunState::kNoProgram);
}

TEST(MachineTest, EbreakPausesAndAdvances) {
  BufferedConsole console;
  Machine machine(MachineConfig{}, console);
  ASSERT_TRUE(machine.Load(MakeImage(
      Xlen::k32, {{Opcode::kEbreak}, {Opcode::kAddi, kA0, kZero, 0, 1}})));
  RunResult result = machine.Run(10);
  EXPECT_EQ(result.status, StepStatus::kEbreak);
  EXPECT_EQ(machine.state().pc, kTextBase + 4);
  EXPECT_EQ(machine.run_state(), RunState::kReady);
}

TEST(MachineTest, BreakpointsStopBeforeExecutionButNotOnResume) {
  BufferedConsole console;
  Machine machine(MachineConfig{}, console);
  ASSERT_TRUE(
      machine.Load(MakeImage(Xlen::k32, {
                                            {Opcode::kAddi, kA0, kA0, 0, 1},
                                            {Opcode::kJal, kZero, 0, 0, -4},
                                        })));
  const std::unordered_set<uint64_t> breakpoints = {kTextBase};
  RunResult result = machine.Run(100, &breakpoints);
  EXPECT_TRUE(result.hit_breakpoint);
  EXPECT_EQ(result.steps, 2u);
  EXPECT_EQ(machine.state().ReadReg(kA0), 1u);
  result = machine.Run(100, &breakpoints);
  EXPECT_TRUE(result.hit_breakpoint);
  EXPECT_EQ(machine.state().ReadReg(kA0), 2u);
  // A chunked continuation must stop immediately on the breakpoint.
  result = machine.Run(100, &breakpoints, /*skip_breakpoint_at_start=*/false);
  EXPECT_TRUE(result.hit_breakpoint);
  EXPECT_EQ(result.steps, 0u);
  EXPECT_EQ(machine.state().ReadReg(kA0), 2u);
}

TEST(MachineTest, FallingOffTheProgramFaults) {
  BufferedConsole console;
  Machine machine(MachineConfig{}, console);
  ASSERT_TRUE(machine.Load(
      MakeImage(Xlen::k32, {{Opcode::kJal, kZero, 0, 0, 0x1000}})));
  const RunResult result = machine.Run(10);
  EXPECT_EQ(result.status, StepStatus::kFaulted);
  ASSERT_TRUE(machine.last_trap());
  EXPECT_EQ(machine.last_trap()->cause, TrapCause::kInstructionAccessFault);
  EXPECT_EQ(machine.Step().status, StepStatus::kNotRunnable);
}

TEST(MachineTest, ResetRestoresMemoryAndRegisters) {
  BufferedConsole console;
  Machine machine(MachineConfig{}, console);
  ASSERT_TRUE(machine.Load(
      MakeImage(Xlen::k32,
                {
                    {Opcode::kLui, kA0, 0, 0, static_cast<int64_t>(kDataBase)},
                    {Opcode::kSw, 0, kA0, kA0, 0},
                },
                "ABCD")));
  machine.Run(2);
  EXPECT_EQ(*machine.bus().Read32(kDataBase), kDataBase);
  machine.Reset();
  EXPECT_EQ(*machine.bus().Read32(kDataBase), 0x44434241u);
  EXPECT_EQ(machine.state().ReadReg(kA0), 0u);
  EXPECT_EQ(machine.state().pc, kTextBase);
  EXPECT_EQ(machine.state().instret, 0u);
}

}  // namespace
}  // namespace rvsim
