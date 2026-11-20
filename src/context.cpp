#include "context.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>

#include "ui/design_tokens.h"
#include "ui/riscv_language.h"

namespace rvsim {
namespace {

// Opaque per-line tag stored as TextEditor user data; it follows the line
// as text is inserted or removed above it.
int kBreakpointTag = 0;

constexpr std::string_view kWelcomeProgram = R"(# Welcome to rvsim!
# Assemble with Ctrl+B, run with F5, step with F11.
# Click the gutter (or press F9) to toggle a breakpoint.

    .data
greeting:   .asciz "Hello from RISC-V!\n"
prompt:     .asciz "Sum of 1..n, enter n: "
result:     .asciz "Sum = "

    .text
    .globl main
main:
    la      a0, greeting
    li      a7, 4               # PrintString
    ecall

    la      a0, prompt
    li      a7, 4
    ecall
    li      a7, 5               # ReadInt -> a0
    ecall

    mv      t0, a0              # n
    li      t1, 0               # sum
loop:
    blez    t0, done
    add     t1, t1, t0
    addi    t0, t0, -1
    j       loop

done:
    la      a0, result
    li      a7, 4
    ecall
    mv      a0, t1
    li      a7, 1               # PrintInt
    ecall
    li      a0, '\n'
    li      a7, 11              # PrintChar
    ecall

    li      a0, 0
    li      a7, 93              # Exit2
    ecall
)";

}  // namespace

Context::Context()
    : clear_color_(ui::Colors().surface_base),
      controller_(std::make_unique<SimulationController>(config_)),
      snapshot_(controller_->snapshot()) {
  text_editor_.SetLanguage(ui::RiscvLanguage());
  text_editor_.SetPalette(ui::RiscvEditorPalette());
  text_editor_.SetTabSize(8);
  text_editor_.SetInsertSpacesOnTabs(true);
  text_editor_.SetShowMatchingBrackets(true);
  text_editor_.SetShowWhitespacesEnabled(false);
  text_editor_.SetText(kWelcomeProgram);
  text_editor_.SetChangeCallback([this] { source_dirty_ = true; });
  source_dirty_ = true;
}

void Context::BeginFrame() {
  stopped_this_frame_ = false;
  program_changed_ = false;
  snapshot_ = controller_->snapshot();
  if (snapshot_->version == last_version_) {
    return;
  }
  last_version_ = snapshot_->version;

  if (snapshot_->program_generation != last_generation_) {
    last_generation_ = snapshot_->program_generation;
    assemble_pending_ = false;
    program_changed_ = true;
    UpdateEditorMarkers();
  }
  const SimState state = snapshot_->state;
  if (state != last_state_) {
    if (last_state_ == SimState::kRunning || state == SimState::kPaused ||
        state == SimState::kExited || state == SimState::kFaulted) {
      stopped_this_frame_ = true;
    }
    last_state_ = state;
  }
  if (snapshot_->stop_reason == StopReason::kStep) {
    stopped_this_frame_ = true;
  }
  const bool locked =
      state == SimState::kRunning || state == SimState::kWaitingForInput;
  text_editor_.SetReadOnlyEnabled(locked);
}

bool Context::can_step() const {
  const SimState state = snapshot_->state;
  return state == SimState::kPaused || state == SimState::kNoProgram;
}

bool Context::can_run() const {
  const SimState state = snapshot_->state;
  return state != SimState::kRunning && state != SimState::kWaitingForInput;
}

bool Context::can_edit_machine() const {
  return has_program() && snapshot_->state != SimState::kRunning;
}

std::optional<int> Context::pc_line() const {
  if (!snapshot_->program || source_dirty_) {
    return std::nullopt;
  }
  if (auto line =
          snapshot_->program->source_map.LineForAddress(snapshot_->cpu.pc)) {
    return *line - 1;
  }
  return std::nullopt;
}

void Context::Assemble() {
  SyncBreakpoints();
  controller_->Assemble(text_editor_.GetText(), config_);
  source_dirty_ = false;
  assemble_pending_ = true;
}

void Context::EnsureAssembled() {
  if (source_dirty_ || (!has_program() && !assemble_pending_)) {
    Assemble();
  }
}

void Context::RunOrContinue() {
  if (!can_run()) return;
  const SimState state = snapshot_->state;
  if (state == SimState::kExited || state == SimState::kFaulted) {
    if (!source_dirty_) controller_->Reset();
  }
  EnsureAssembled();
  controller_->Run();
}

void Context::Pause() { controller_->Pause(); }

void Context::ToggleRunPause() {
  if (is_running() || snapshot_->state == SimState::kWaitingForInput) {
    Pause();
  } else {
    RunOrContinue();
  }
}

void Context::StepIn() {
  if (!can_step()) return;
  EnsureAssembled();
  controller_->StepIn();
}

void Context::Reset() {
  if (source_dirty_) {
    Assemble();  // Reset to the current source.
  } else {
    controller_->Reset();
  }
}

void Context::SetArchitecture(Xlen xlen) {
  if (config_.xlen == xlen) return;
  config_.xlen = xlen;
  if (has_program() || !snapshot_->diagnostics.empty()) {
    Assemble();
  } else {
    controller_->SetConfig(config_);
  }
}

void Context::SetSpeedHz(uint64_t hz) {
  speed_hz_ = hz;
  controller_->SetSpeedHz(hz);
}

// ---------------------------------------------------------------- breakpoints

bool Context::HasBreakpointAtLine(int line) const {
  if (line < 0 || line >= text_editor_.GetLineCount()) return false;
  return text_editor_.GetUserData(line) == &kBreakpointTag;
}

void Context::ToggleBreakpointAtLine(int line) {
  if (line < 0 || line >= text_editor_.GetLineCount()) return;
  text_editor_.SetUserData(
      line, HasBreakpointAtLine(line) ? nullptr : &kBreakpointTag);
  SyncBreakpoints();
}

void Context::ToggleBreakpointAtCursor() {
  int line = 0;
  int column = 0;
  text_editor_.GetMainCursor(line, column);
  ToggleBreakpointAtLine(line);
}

void Context::ToggleBreakpointAtAddress(uint64_t address) {
  if (!snapshot_->program) return;
  if (auto line = snapshot_->program->source_map.LineForAddress(address)) {
    ToggleBreakpointAtLine(*line - 1);
  }
}

bool Context::HasBreakpointAtAddress(uint64_t address) const {
  if (!snapshot_->program) return false;
  const auto line = snapshot_->program->source_map.LineForAddress(address);
  if (!line) return false;
  // Breakpoints apply to the first instruction emitted for a line.
  const auto first = snapshot_->program->source_map.AddressForLine(*line);
  return first == address && HasBreakpointAtLine(*line - 1);
}

void Context::ClearBreakpoints() {
  for (int line = 0; line < text_editor_.GetLineCount(); ++line) {
    if (HasBreakpointAtLine(line)) text_editor_.SetUserData(line, nullptr);
  }
  SyncBreakpoints();
}

bool Context::LineHasCode(int line) const {
  return snapshot_->program &&
         snapshot_->program->source_map.AddressForLine(line + 1).has_value();
}

void Context::SyncBreakpoints() {
  std::vector<int> lines;
  text_editor_.IterateUserData([&](int line, void* data) {
    if (data == &kBreakpointTag) lines.push_back(line + 1);
  });
  std::ranges::sort(lines);
  controller_->SetSourceBreakpoints(std::move(lines));
}

// ---------------------------------------------------------------- navigation

void Context::JumpToSource(int line, int column) {
  const int line0 = std::clamp(line - 1, 0, text_editor_.GetLineCount() - 1);
  text_editor_.SetCursor(line0, std::max(column - 1, 0));
  text_editor_.ScrollToLine(line0, TextEditor::Scroll::alignMiddle);
  editor_focus_requested_ = true;
}

bool Context::ConsumeEditorFocusRequest() {
  const bool requested = editor_focus_requested_;
  editor_focus_requested_ = false;
  return requested;
}

void Context::UpdateEditorMarkers() {
  text_editor_.ClearMarkers();
  std::map<int, std::pair<Severity, std::string>> by_line;
  for (const Diagnostic& d : snapshot_->diagnostics) {
    auto& [severity, text] = by_line[d.line];
    if (text.empty()) {
      severity = d.severity;
    } else {
      text += '\n';
      if (d.severity == Severity::kError) severity = Severity::kError;
    }
    text += std::format("{} (col {}): {}",
                        d.severity == Severity::kError ? "error" : "warning",
                        d.column, d.message);
  }
  const ui::Palette& c = ui::Colors();
  for (const auto& [line, entry] : by_line) {
    const ImVec4 base = entry.first == Severity::kError ? c.danger : c.warning;
    const ImU32 gutter = ui::U32(ImVec4(base.x, base.y, base.z, 0.55f));
    const ImU32 text = ui::U32(ImVec4(base.x, base.y, base.z, 0.16f));
    text_editor_.AddMarker(line - 1, gutter, text, entry.second, entry.second);
  }
}

}  // namespace rvsim
