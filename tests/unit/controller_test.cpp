#include "simulator/runtime/controller.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "simulator/core/registers.h"

namespace rvsim {
namespace {

using namespace std::chrono_literals;

constexpr auto kTimeout = 5s;

constexpr std::string_view kHello = R"(
  .data
msg: .asciz "hello\n"
  .text
main:
  la a0, msg
  li a7, 4
  ecall
  li a0, 3
  li a7, 93
  ecall
)";

constexpr std::string_view kCountLoop = R"(
main:
  li t0, 0
loop:
  addi t0, t0, 1
  j loop
)";

bool Wait(SimulationController& c,
          const std::function<bool(const UiSnapshot&)>& predicate) {
  return c.WaitForSnapshot(predicate, kTimeout);
}

bool WaitState(SimulationController& c, SimState state) {
  return Wait(c, [state](const UiSnapshot& s) { return s.state == state; });
}

TEST(ControllerTest, InitialSnapshotHasNoProgram) {
  SimulationController controller;
  const auto snap = controller.snapshot();
  ASSERT_NE(snap, nullptr);
  EXPECT_EQ(snap->state, SimState::kNoProgram);
  EXPECT_EQ(snap->program, nullptr);
}

TEST(ControllerTest, AssembleAndRunToExit) {
  SimulationController controller;
  controller.Assemble(std::string(kHello), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  auto snap = controller.snapshot();
  EXPECT_EQ(snap->stop_reason, StopReason::kAssembled);
  ASSERT_NE(snap->program, nullptr);
  EXPECT_EQ(snap->cpu.pc, snap->program->entry_point);
  EXPECT_EQ(snap->program_generation, 1u);

  controller.Run();
  ASSERT_TRUE(WaitState(controller, SimState::kExited));
  snap = controller.snapshot();
  EXPECT_EQ(snap->console, "hello\n");
  EXPECT_EQ(snap->exit_code, 3);
  EXPECT_EQ(snap->stop_reason, StopReason::kExited);
}

TEST(ControllerTest, AssemblyErrorsAreReported) {
  SimulationController controller;
  controller.Assemble("main:\n  bogus a0\n", MachineConfig{});
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.stop_reason == StopReason::kAssemblyFailed;
  }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->state, SimState::kNoProgram);
  ASSERT_EQ(snap->diagnostics.size(), 1u);
  EXPECT_EQ(snap->diagnostics[0].line, 2);
  EXPECT_FALSE(snap->message.empty());
}

TEST(ControllerTest, StepRecordsPreviousState) {
  SimulationController controller;
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  const uint64_t entry = controller.snapshot()->cpu.pc;
  controller.StepIn();
  controller.StepIn();
  ASSERT_TRUE(
      Wait(controller, [](const UiSnapshot& s) { return s.cpu.instret == 2; }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->stop_reason, StopReason::kStep);
  EXPECT_EQ(snap->cpu.pc, entry + 8);
  EXPECT_EQ(snap->prev_cpu.pc, entry + 4);
  EXPECT_EQ(snap->cpu.ReadReg(reg::kT0), 1u);
  EXPECT_EQ(snap->prev_cpu.ReadReg(reg::kT0), 0u);
}

TEST(ControllerTest, BreakpointsStopEveryIteration) {
  SimulationController controller;
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  const uint64_t loop = *controller.snapshot()->program->LookupSymbol("loop");
  controller.SetBreakpoints({loop});
  for (uint64_t iteration = 1; iteration <= 3; ++iteration) {
    controller.Run();
    ASSERT_TRUE(Wait(controller,
                     [&](const UiSnapshot& s) {
                       return s.stop_reason == StopReason::kBreakpoint &&
                              s.cpu.ReadReg(reg::kT0) == iteration - 1;
                     }))
        << "iteration " << iteration;
    EXPECT_EQ(controller.snapshot()->cpu.pc, loop);
  }
}

TEST(ControllerTest, SourceBreakpointsApplyToNextAssembly) {
  SimulationController controller;
  // Line 5 of kCountLoop is "addi t0, t0, 1" (line 4 is the label); set
  // before assembling and run immediately, as the IDE does on F5.
  controller.SetSourceBreakpoints({5});
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  controller.Run();
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.stop_reason == StopReason::kBreakpoint;
  }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->cpu.pc, *snap->program->LookupSymbol("loop"));
  EXPECT_EQ(snap->cpu.ReadReg(reg::kT0), 0u);
}

TEST(ControllerTest, PauseStopsInfiniteLoop) {
  SimulationController controller;
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.Run();
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.state == SimState::kRunning && s.cpu.instret > 10000;
  }));
  EXPECT_GT(controller.snapshot()->effective_hz, 0.0);
  controller.Pause();
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  EXPECT_EQ(controller.snapshot()->stop_reason, StopReason::kPaused);
}

TEST(ControllerTest, WaitsForConsoleInputThenResumes) {
  SimulationController controller;
  controller.Assemble(R"(
main:
  li a7, 5
  ecall
  addi a0, a0, 1
  li a7, 1
  ecall
  li a7, 10
  ecall
)",
                      MachineConfig{.xlen = Xlen::k64});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.Run();
  ASSERT_TRUE(WaitState(controller, SimState::kWaitingForInput));
  controller.SendConsoleInput("41\n");
  ASSERT_TRUE(WaitState(controller, SimState::kExited));
  EXPECT_EQ(controller.snapshot()->console, "41\n42");
}

TEST(ControllerTest, StepIntoInputWaitResumesAsStep) {
  SimulationController controller;
  controller.Assemble("main:\n li a7, 12\n ecall\n nop\n", MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.StepIn();  // li
  controller.StepIn();  // ecall -> waits
  ASSERT_TRUE(WaitState(controller, SimState::kWaitingForInput));
  controller.SendConsoleInput("x");
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.state == SimState::kPaused && s.cpu.instret == 2;
  }));
  EXPECT_EQ(controller.snapshot()->cpu.ReadReg(reg::kA0), uint64_t{'x'});
}

TEST(ControllerTest, SpeedLimitThrottlesExecution) {
  SimulationController controller;
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.SetSpeedHz(100);
  controller.Run();
  std::this_thread::sleep_for(300ms);
  controller.Pause();
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  const uint64_t instret = controller.snapshot()->cpu.instret;
  EXPECT_GT(instret, 5u);
  EXPECT_LT(instret, 100u);
  EXPECT_EQ(controller.snapshot()->speed_hz, 100u);
}

TEST(ControllerTest, MemoryViewportsMirrorRam) {
  SimulationController controller;
  controller.Assemble(std::string(kHello), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  const uint64_t msg = *controller.snapshot()->program->LookupSymbol("msg");
  controller.SetMemoryViewports({{msg, 8}, {0x7FFFFF00, 16}});
  ASSERT_TRUE(Wait(controller,
                   [](const UiSnapshot& s) { return s.memory.size() == 2; }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->memory[0].address, msg);
  EXPECT_EQ(std::string(snap->memory[0].bytes.begin(),
                        snap->memory[0].bytes.begin() + 5),
            "hello");
  EXPECT_EQ(snap->memory[0].mapped[0], 1);
  EXPECT_EQ(snap->memory[1].mapped[0], 0);
}

TEST(ControllerTest, WritesWhilePaused) {
  SimulationController controller;
  controller.Assemble(std::string(kHello), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  const uint64_t msg = *controller.snapshot()->program->LookupSymbol("msg");
  controller.WriteRegister(reg::kS0, 0x1234);
  controller.WriteMemory(msg, {'J'});
  controller.Run();
  ASSERT_TRUE(WaitState(controller, SimState::kExited));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->cpu.ReadReg(reg::kS0), 0x1234u);
  EXPECT_EQ(snap->console, "Jello\n");
}

TEST(ControllerTest, ResetRestoresProgram) {
  SimulationController controller;
  controller.Assemble(std::string(kHello), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.Run();
  ASSERT_TRUE(WaitState(controller, SimState::kExited));
  controller.Reset();
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.stop_reason == StopReason::kReset;
  }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->state, SimState::kPaused);
  EXPECT_EQ(snap->cpu.instret, 0u);
  EXPECT_TRUE(snap->console.empty());
}

TEST(ControllerTest, EbreakPauses) {
  SimulationController controller;
  controller.Assemble("main:\n nop\n ebreak\n nop\n", MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.Run();
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.stop_reason == StopReason::kEbreak;
  }));
}

TEST(ControllerTest, FaultIsReported) {
  SimulationController controller;
  controller.Assemble("main:\n j main + 0x1000\n", MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.Run();
  ASSERT_TRUE(WaitState(controller, SimState::kFaulted));
  EXPECT_NE(controller.snapshot()->message.find("access fault"),
            std::string::npos);
}

TEST(ControllerTest, ConfigChangeUnloadsProgram) {
  SimulationController controller;
  controller.Assemble(std::string(kHello), MachineConfig{});
  ASSERT_TRUE(WaitState(controller, SimState::kPaused));
  controller.SetConfig(MachineConfig{.xlen = Xlen::k64});
  ASSERT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.stop_reason == StopReason::kConfigChanged;
  }));
  const auto snap = controller.snapshot();
  EXPECT_EQ(snap->state, SimState::kNoProgram);
  EXPECT_EQ(snap->config.xlen, Xlen::k64);
  EXPECT_EQ(snap->cpu.xlen, Xlen::k64);
}

TEST(ControllerTest, DestroyWhileRunningOrWaiting) {
  {
    SimulationController controller;
    controller.Assemble(std::string(kCountLoop), MachineConfig{});
    controller.Run();
    std::this_thread::sleep_for(20ms);
  }
  {
    SimulationController controller;
    controller.Assemble("main:\n li a7, 5\n ecall\n", MachineConfig{});
    controller.Run();
    ASSERT_TRUE(WaitState(controller, SimState::kWaitingForInput));
  }
}

TEST(ControllerTest, ConcurrentCommandsAndSnapshotReaders) {
  SimulationController controller;
  controller.Assemble(std::string(kCountLoop), MachineConfig{});
  std::atomic<bool> done{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      uint64_t seen_version = 0;
      int i = 0;
      while (!done.load()) {
        switch ((i++ + t) % 6) {
          case 0:
            controller.Run();
            break;
          case 1:
            controller.Pause();
            break;
          case 2:
            controller.StepIn();
            break;
          case 3:
            controller.SetMemoryViewports({{0x400000, 64}});
            break;
          case 4:
            controller.SetBreakpoints({0x400004});
            break;
          case 5:
            controller.SendConsoleInput("1\n");
            break;
        }
        const auto snap = controller.snapshot();
        EXPECT_GE(snap->version, seen_version);
        seen_version = snap->version;
      }
    });
  }
  std::this_thread::sleep_for(200ms);
  done = true;
  for (std::thread& thread : threads) thread.join();
  controller.Pause();
  EXPECT_TRUE(Wait(controller, [](const UiSnapshot& s) {
    return s.state != SimState::kRunning;
  }));
}

}  // namespace
}  // namespace rvsim
