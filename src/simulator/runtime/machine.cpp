#include "simulator/runtime/machine.h"

#include <format>

#include "simulator/core/decoder.h"
#include "simulator/core/executor.h"
#include "simulator/core/registers.h"
#include "simulator/env/rars_env.h"

namespace rvsim {

Machine::Machine(const MachineConfig& config, ConsoleIo& console)
    : config_(config), console_(console) {
  CreateDevices();
}

Machine::~Machine() = default;

void Machine::CreateDevices() {
  bus_ = std::make_unique<MemoryBus>(config_.max_pages);
  bus_->set_strict_alignment(config_.strict_alignment);
  switch (config_.environment) {
    case EnvironmentKind::kRars:
      environment_ = std::make_unique<RarsEnvironment>(console_);
      break;
  }
  state_ = CpuState{};
  state_.xlen = config_.xlen;
  state_.extensions = config_.extensions;
}

void Machine::Configure(const MachineConfig& config) {
  config_ = config;
  Unload();
  CreateDevices();
}

Result<void> Machine::Load(ProgramImage image) {
  if (image.xlen != config_.xlen) {
    return MakeError(ErrorCode::kInvalidArgument,
                     std::format("program is {} but the machine is {}",
                                 ToString(image.xlen), ToString(config_.xlen)));
  }
  image_ = std::move(image);
  Reset();
  if (run_state_ == RunState::kFaulted) {
    std::string message = fault_message_;
    Unload();
    return MakeError(ErrorCode::kLoadError, std::move(message));
  }
  return {};
}

void Machine::Unload() {
  image_.reset();
  bus_->ram().Clear();
  state_.ResetArchState();
  run_state_ = RunState::kNoProgram;
  exit_code_ = 0;
  last_trap_.reset();
  fault_message_.clear();
}

void Machine::Reset() {
  bus_->ram().Clear();
  state_.ResetArchState();
  state_.xlen = config_.xlen;
  state_.extensions = config_.extensions;
  exit_code_ = 0;
  last_trap_.reset();
  fault_message_.clear();
  if (!image_) {
    run_state_ = RunState::kNoProgram;
    return;
  }
  run_state_ = RunState::kReady;
  for (const Segment& segment : image_->segments) {
    if (!bus_->ram().WriteRegion(segment.address, segment.data) ||
        !bus_->ram().FillRegion(segment.address + segment.data.size(),
                                segment.mem_size - segment.data.size(), 0)) {
      Fault(std::format("segment '{}' at {:#x} exceeds the memory budget",
                        segment.name, segment.address),
            std::nullopt);
      return;
    }
  }
  state_.SetPc(image_->entry_point);
  state_.WriteReg(reg::kSp, image_->initial_sp);
  if (image_->initial_gp) {
    state_.WriteReg(reg::kGp, *image_->initial_gp);
  }
  environment_->Reset(image_->heap_start);
}

void Machine::Fault(std::string message, std::optional<Trap> trap) {
  run_state_ = RunState::kFaulted;
  fault_message_ = std::move(message);
  last_trap_ = trap;
}

StepResult Machine::Step() {
  if (run_state_ != RunState::kReady) {
    return {StepStatus::kNotRunnable};
  }
  const uint64_t pc = state_.pc;
  if ((pc & 0x3) != 0) {
    const Trap trap{TrapCause::kInstructionAddressMisaligned, pc};
    Fault(std::format("{} at pc={:#x}", ToString(trap.cause), pc), trap);
    return {StepStatus::kFaulted};
  }
  auto raw = bus_->FetchInstruction32(pc);
  if (!raw) {
    Fault(std::format("{} at pc={:#x}", ToString(raw.error().cause), pc),
          raw.error());
    return {StepStatus::kFaulted};
  }
  const DecodedInst inst = Decode(*raw, state_.xlen);
  const ExecResult result = Execute(state_, *bus_, inst);

  switch (result.status) {
    case ExecStatus::kOk:
      ++state_.cycle;
      ++state_.instret;
      return {StepStatus::kRetired};
    case ExecStatus::kEbreak:
      state_.SetPc(pc + inst.length);
      ++state_.cycle;
      ++state_.instret;
      return {StepStatus::kEbreak};
    case ExecStatus::kTrap:
      Fault(std::format("{} (tval={:#x}) at pc={:#x}",
                        ToString(result.trap.cause), result.trap.tval, pc),
            result.trap);
      return {StepStatus::kFaulted};
    case ExecStatus::kEcall:
      break;
  }

  const EcallResult ecall = environment_->HandleEcall(state_, *bus_);
  switch (ecall.action) {
    case EcallAction::kContinue:
      state_.SetPc(pc + inst.length);
      ++state_.cycle;
      ++state_.instret;
      return {StepStatus::kRetired};
    case EcallAction::kWaitForInput:
      return {StepStatus::kWaitingForInput};
    case EcallAction::kExit:
      ++state_.cycle;
      ++state_.instret;
      run_state_ = RunState::kExited;
      exit_code_ = ecall.exit_code;
      return {StepStatus::kExited};
    case EcallAction::kFault:
      Fault(std::format("{} at pc={:#x}", ecall.message, pc),
            Trap{TrapCause::kEcallFromUMode, 0});
      return {StepStatus::kFaulted};
  }
  return {StepStatus::kFaulted};
}

RunResult Machine::Run(uint64_t max_steps,
                       const std::unordered_set<uint64_t>* breakpoints,
                       bool skip_breakpoint_at_start) {
  RunResult result;
  const bool check_breakpoints =
      breakpoints != nullptr && !breakpoints->empty();
  while (result.steps < max_steps) {
    if (check_breakpoints && (result.steps > 0 || !skip_breakpoint_at_start) &&
        breakpoints->contains(state_.pc)) {
      result.hit_breakpoint = true;
      return result;
    }
    const StepResult step = Step();
    result.status = step.status;
    if (step.status == StepStatus::kRetired) {
      ++result.steps;
      continue;
    }
    if (step.status == StepStatus::kEbreak) {
      ++result.steps;
    }
    return result;
  }
  return result;
}

}  // namespace rvsim
