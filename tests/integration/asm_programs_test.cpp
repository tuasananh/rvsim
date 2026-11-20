#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "simulator/env/buffered_console.h"
#include "simulator/loader/assembler.h"
#include "simulator/runtime/machine.h"

namespace rvsim {
namespace {

std::string ReadTestData(const std::string& name) {
  std::ifstream file(std::string(RVSIM_TESTDATA_DIR) + "/" + name,
                     std::ios::binary);
  EXPECT_TRUE(file) << "missing test data " << name;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

struct RunOutcome {
  RunState state = RunState::kNoProgram;
  int64_t exit_code = -1;
  std::string output;
  std::string fault;
};

RunOutcome RunFile(const std::string& name, Xlen xlen,
                   std::string_view input = {}) {
  const AssemblyResult assembled =
      Assemble(ReadTestData(name), AssemblerOptions{.xlen = xlen});
  RunOutcome outcome;
  if (!assembled.ok()) {
    for (const Diagnostic& d : assembled.diagnostics) {
      ADD_FAILURE() << name << ":" << d.line << ":" << d.column << ": "
                    << d.message;
    }
    return outcome;
  }
  BufferedConsole console;
  console.PushInput(input);
  console.CloseInput();
  Machine machine(MachineConfig{.xlen = xlen}, console);
  EXPECT_TRUE(machine.Load(*assembled.image));
  machine.Run(50'000'000);
  outcome.state = machine.run_state();
  outcome.exit_code = machine.exit_code();
  outcome.output = console.combined_text();
  outcome.fault = machine.fault_message();
  return outcome;
}

class AsmProgramsTest : public ::testing::TestWithParam<Xlen> {};

TEST_P(AsmProgramsTest, Fibonacci) {
  const RunOutcome r = RunFile("fibonacci.s", GetParam());
  EXPECT_EQ(r.state, RunState::kExited) << r.fault;
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.output,
            "fib: 0 1 1 2 3 5 8 13 21 34 55 89 144 233 377 610 \n"
            "recursive fib(15) = 610\n");
}

TEST_P(AsmProgramsTest, RecursiveFactorial) {
  const RunOutcome r = RunFile("factorial.s", GetParam());
  EXPECT_EQ(r.state, RunState::kExited) << r.fault;
  EXPECT_EQ(r.exit_code, 3628800 & 0xFF);
  EXPECT_EQ(r.output,
            "1! = 1\n2! = 2\n3! = 6\n4! = 24\n5! = 120\n6! = 720\n"
            "7! = 5040\n8! = 40320\n9! = 362880\n10! = 3628800\n");
}

TEST_P(AsmProgramsTest, Quicksort) {
  const RunOutcome r = RunFile("quicksort.s", GetParam());
  EXPECT_EQ(r.state, RunState::kExited) << r.fault;
  EXPECT_EQ(r.output, "-32 -7 -1 0 2 5 7 7 9 15 23 32 34 41 62 100 \n");
}

TEST_P(AsmProgramsTest, StringIo) {
  const RunOutcome r = RunFile("string_io.s", GetParam(), "Ada\n21\n");
  EXPECT_EQ(r.state, RunState::kExited) << r.fault;
  EXPECT_EQ(r.output, "name? Hello, Ada\ntwice: 42\n");
}

INSTANTIATE_TEST_SUITE_P(BothXlens, AsmProgramsTest,
                         ::testing::Values(Xlen::k32, Xlen::k64),
                         [](const ::testing::TestParamInfo<Xlen>& info) {
                           return info.param == Xlen::k32 ? "RV32" : "RV64";
                         });

TEST(AsmProgramsRv64Test, DoublewordArithmetic) {
  const RunOutcome r = RunFile("rv64_sum.s", Xlen::k64);
  EXPECT_EQ(r.state, RunState::kExited) << r.fault;
  EXPECT_EQ(r.output, "0x00000002ffffffff\n-2147483648");
}

TEST(AsmProgramsRv64Test, RejectedOnRv32) {
  const AssemblyResult assembled =
      Assemble(ReadTestData("rv64_sum.s"), AssemblerOptions{});
  EXPECT_FALSE(assembled.ok());
}

}  // namespace
}  // namespace rvsim
