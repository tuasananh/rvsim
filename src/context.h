#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "TextEditor.h"
#include "imgui.h"
#include "simulator/runtime/controller.h"

namespace rvsim {

enum class NumberFormat : uint8_t { kHex, kSigned, kUnsigned };

// Shared UI-thread application state: the source editor, the simulation
// controller, the frame-consistent UiSnapshot, and cross-panel UI state.
// Everything here is accessed only from the UI thread.
class Context {
 public:
  Context();

  const ImVec4& clear_color() const { return clear_color_; }

  void set_monospace_font(ImFont* font) { monospace_font_ = font; }
  ImFont* monospace_font() const { return monospace_font_; }
  void set_sans_font(ImFont* font) { sans_font_ = font; }
  ImFont* sans_font() const { return sans_font_; }

  TextEditor& text_editor() { return text_editor_; }
  const TextEditor& text_editor() const { return text_editor_; }
  SimulationController& controller() { return *controller_; }

  // Fetches the latest snapshot once per frame and reacts to transitions
  // (new program, stops, run state). Call before rendering any panel.
  void BeginFrame();
  const UiSnapshot& snapshot() const { return *snapshot_; }

  // ---- derived state ----
  bool has_program() const { return snapshot_->program != nullptr; }
  bool is_running() const { return snapshot_->state == SimState::kRunning; }
  bool can_step() const;
  bool can_run() const;
  bool can_edit_machine() const;
  bool source_dirty() const { return source_dirty_; }
  // 0-based editor line of the current pc, if it maps to source.
  std::optional<int> pc_line() const;

  // ---- actions ----
  void Assemble();
  void RunOrContinue();
  void Pause();
  void ToggleRunPause();
  void StepIn();
  void Reset();
  void SetArchitecture(Xlen xlen);
  void SetSpeedHz(uint64_t hz);
  uint64_t speed_hz() const { return speed_hz_; }
  const MachineConfig& selected_config() const { return config_; }

  // ---- breakpoints (stored per editor line, follow edits) ----
  bool HasBreakpointAtLine(int line) const;
  void ToggleBreakpointAtLine(int line);
  void ToggleBreakpointAtCursor();
  void ToggleBreakpointAtAddress(uint64_t address);
  bool HasBreakpointAtAddress(uint64_t address) const;
  void ClearBreakpoints();
  // True if `line` (0-based) produced code in the loaded program.
  bool LineHasCode(int line) const;

  // ---- navigation ----
  // Moves the editor cursor to a 1-based line/column and focuses it.
  void JumpToSource(int line, int column);
  void RequestEditorFocus() { editor_focus_requested_ = true; }
  bool ConsumeEditorFocusRequest();
  void set_editor_focused(bool focused) { editor_focused_ = focused; }
  bool editor_focused() const { return editor_focused_; }

  // ---- UI preferences ----
  bool follow_pc = true;
  NumberFormat register_format = NumberFormat::kHex;
  bool show_about = false;
  bool reset_layout_requested = false;
  bool quit_requested = false;
  // Set for one frame when a run stops so panels can react (e.g. scroll).
  bool stopped_this_frame() const { return stopped_this_frame_; }
  bool program_changed_this_frame() const { return program_changed_; }

 private:
  void SyncBreakpoints();
  void UpdateEditorMarkers();
  void EnsureAssembled();

  ImVec4 clear_color_;
  ImFont* monospace_font_ = nullptr;
  ImFont* sans_font_ = nullptr;
  TextEditor text_editor_;
  MachineConfig config_;
  uint64_t speed_hz_ = 0;
  std::unique_ptr<SimulationController> controller_;
  std::shared_ptr<const UiSnapshot> snapshot_;
  uint64_t last_version_ = 0;
  uint64_t last_generation_ = 0;
  SimState last_state_ = SimState::kNoProgram;
  bool source_dirty_ = true;
  bool assemble_pending_ = false;
  bool editor_focus_requested_ = false;
  bool editor_focused_ = false;
  bool stopped_this_frame_ = false;
  bool program_changed_ = false;
  std::optional<int> last_pc_line_;
};

}  // namespace rvsim
