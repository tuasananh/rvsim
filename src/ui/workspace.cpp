#include "ui/workspace.h"

#include <array>
#include <format>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "simulator/simulator.h"
#include "ui/design_tokens.h"

namespace rvsim::ui {
namespace {

struct SpeedPreset {
  uint64_t hz;
  const char* label;
};

constexpr std::array<SpeedPreset, 12> kSpeeds = {{
    {1, "1 Hz"},
    {2, "2 Hz"},
    {5, "5 Hz"},
    {10, "10 Hz"},
    {30, "30 Hz"},
    {100, "100 Hz"},
    {1'000, "1 kHz"},
    {10'000, "10 kHz"},
    {100'000, "100 kHz"},
    {1'000'000, "1 MHz"},
    {10'000'000, "10 MHz"},
    {0, "Max"},
}};

int SpeedIndex(uint64_t hz) {
  for (size_t i = 0; i < kSpeeds.size(); ++i) {
    if (kSpeeds[i].hz == hz) return static_cast<int>(i);
  }
  return static_cast<int>(kSpeeds.size()) - 1;
}

ImVec4 StateColor(SimState state) {
  const Palette& c = Colors();
  switch (state) {
    case SimState::kRunning:
      return c.pill_running;
    case SimState::kPaused:
      return c.pill_paused;
    case SimState::kWaitingForInput:
      return c.pill_waiting;
    case SimState::kExited:
      return c.pill_halted;
    case SimState::kFaulted:
      return c.pill_error;
    case SimState::kNoProgram:
      break;
  }
  return c.border_strong;
}

// Rounded, filled status badge with dark text, vertically centered on the
// current line.
void Pill(std::string_view text, const ImVec4& background) {
  const ImVec2 text_size =
      ImGui::CalcTextSize(text.data(), text.data() + text.size());
  const ImVec2 padding(spacing::kSm, spacing::kUnit * 0.5f);
  const ImVec2 size(text_size.x + padding.x * 2, text_size.y + padding.y * 2);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float offset_y = (ImGui::GetFrameHeight() - size.y) * 0.5f;
  const ImVec2 min(pos.x, pos.y + offset_y);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y),
                      U32(background), size.y * 0.5f);
  draw->AddText(ImVec2(min.x + padding.x, min.y + padding.y),
                U32(Colors().pill_text), text.data(),
                text.data() + text.size());
  ImGui::Dummy(ImVec2(size.x, ImGui::GetFrameHeight()));
}

void Separator() {
  ImGui::SameLine();
  ImGui::TextColored(Colors().border_strong, "|");
  ImGui::SameLine();
}

// Tooltip on hover, also when the item is disabled.
void Hint(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                           ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip("%s", text);
  }
}

bool PrimaryButton(const char* label, bool primary) {
  const Palette& c = Colors();
  if (primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, c.primary_button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.primary_button_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.primary_button_active);
  }
  const bool pressed = ImGui::Button(label);
  if (primary) {
    ImGui::PopStyleColor(3);
  }
  return pressed;
}

}  // namespace

Workspace::Workspace(std::shared_ptr<Context> context)
    : context_(std::move(context)) {}

Window* Workspace::FindPanel(const char* name) const {
  for (Window* panel : panels_) {
    if (std::string_view(panel->name()) == name) return panel;
  }
  return nullptr;
}

void Workspace::Render() {
  HandleShortcuts();
  RenderMenuBar();
  RenderToolbar();
  RenderStatusBar();
  RenderDockSpace();
  RenderAboutDialog();
}

void Workspace::HandleShortcuts() {
  Context& ctx = *context_;
  const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;
  // Function keys never conflict with typing. Ctrl chords are skipped while
  // a plain text field (not the source editor) is receiving text input.
  const bool typing_in_field =
      ImGui::GetIO().WantTextInput && !ctx.editor_focused();

  if (ImGui::Shortcut(ImGuiKey_F5, route)) ctx.ToggleRunPause();
  if (ImGui::Shortcut(ImGuiKey_F11, route)) ctx.StepIn();
  if (ImGui::Shortcut(ImGuiKey_F9, route)) ctx.ToggleBreakpointAtCursor();
  if (!typing_in_field) {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, route)) ctx.Assemble();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, route)) ctx.Reset();
  }
}

void Workspace::RenderMenuBar() {
  Context& ctx = *context_;
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File")) {
#ifndef __EMSCRIPTEN__
    if (ImGui::MenuItem("Quit", "Alt+F4")) ctx.quit_requested = true;
#else
    ImGui::MenuItem("Open / Save arrive in Checkpoint 9", nullptr, false,
                    false);
#endif
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Edit")) {
    TextEditor& editor = ctx.text_editor();
    const bool writable = !editor.IsReadOnlyEnabled();
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editor.CanUndo())) {
      editor.Undo();
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editor.CanRedo())) {
      editor.Redo();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false,
                        writable && editor.AnyCursorHasSelection())) {
      editor.Cut();
    }
    if (ImGui::MenuItem("Copy", "Ctrl+C", false,
                        editor.AnyCursorHasSelection())) {
      editor.Copy();
    }
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, writable)) editor.Paste();
    if (ImGui::MenuItem("Select All", "Ctrl+A")) editor.SelectAll();
    ImGui::Separator();
    if (ImGui::MenuItem("Find / Replace", "Ctrl+F")) {
      editor.OpenFindReplaceWindow();
      ctx.RequestEditorFocus();
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("View")) {
    for (Window* panel : panels_) {
      ImGui::MenuItem(panel->name(), nullptr, panel->visible_ptr());
    }
    ImGui::Separator();
    ImGui::MenuItem("Follow PC", nullptr, &ctx.follow_pc);
    if (ImGui::MenuItem("Reset Layout")) ctx.reset_layout_requested = true;
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Simulate")) {
    if (ImGui::MenuItem("Assemble", "Ctrl+B", false, !ctx.is_running())) {
      ctx.Assemble();
    }
    const bool running =
        ctx.is_running() || ctx.snapshot().state == SimState::kWaitingForInput;
    if (ImGui::MenuItem(running ? "Pause" : "Run / Continue", "F5", false,
                        running || ctx.can_run())) {
      ctx.ToggleRunPause();
    }
    if (ImGui::MenuItem("Step Into", "F11", false, ctx.can_step())) {
      ctx.StepIn();
    }
    if (ImGui::MenuItem("Reset", "Ctrl+R", false, ctx.has_program())) {
      ctx.Reset();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Toggle Breakpoint", "F9")) {
      ctx.ToggleBreakpointAtCursor();
    }
    if (ImGui::MenuItem("Clear All Breakpoints")) ctx.ClearBreakpoints();
    ImGui::Separator();
    if (ImGui::BeginMenu("Architecture")) {
      const Xlen xlen = ctx.selected_config().xlen;
      if (ImGui::MenuItem("RV32I", nullptr, xlen == Xlen::k32)) {
        ctx.SetArchitecture(Xlen::k32);
      }
      if (ImGui::MenuItem("RV64I", nullptr, xlen == Xlen::k64)) {
        ctx.SetArchitecture(Xlen::k64);
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Help")) {
    if (Window* demo = FindPanel("ImGuiDemo")) {
      ImGui::MenuItem("Dear ImGui Demo", nullptr, demo->visible_ptr());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("About rvsim")) ctx.show_about = true;
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

void Workspace::RenderToolbar() {
  Context& ctx = *context_;
  const UiSnapshot& snap = ctx.snapshot();
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float height = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollWithMouse;
  const bool open = ImGui::BeginViewportSideBar("##Toolbar", viewport,
                                                ImGuiDir_Up, height, flags);
  if (open) {
    const bool running = ctx.is_running();
    const bool waiting = snap.state == SimState::kWaitingForInput;

    // Machine configuration.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Arch");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("RV64I").x +
                            ImGui::GetFrameHeight() + style.FramePadding.x * 2);
    ImGui::BeginDisabled(running);
    const Xlen xlen = ctx.selected_config().xlen;
    if (ImGui::BeginCombo("##arch", xlen == Xlen::k32 ? "RV32I" : "RV64I")) {
      if (ImGui::Selectable("RV32I", xlen == Xlen::k32)) {
        ctx.SetArchitecture(Xlen::k32);
      }
      if (ImGui::Selectable("RV64I", xlen == Xlen::k64)) {
        ctx.SetArchitecture(Xlen::k64);
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Hint(running ? "Pause the program to change the architecture"
                 : "Register width; changing it re-assembles the program");

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Env");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("RARS / Venus").x +
                            ImGui::GetFrameHeight() + style.FramePadding.x * 2);
    if (ImGui::BeginCombo("##env", "RARS / Venus")) {
      ImGui::Selectable("RARS / Venus", true);
      ImGui::BeginDisabled();
      ImGui::Selectable("Linux / Newlib (Checkpoint 4)", false);
      ImGui::Selectable("Bare-metal M-mode (Checkpoint 8)", false);
      ImGui::EndDisabled();
      ImGui::EndCombo();
    }
    Hint("System call ABI used by ecall (service number in a7)");

    Separator();

    // Execution controls.
    const bool needs_assembly = ctx.source_dirty() || !ctx.has_program();
    ImGui::BeginDisabled(running || waiting);
    if (PrimaryButton("Assemble", needs_assembly)) ctx.Assemble();
    ImGui::EndDisabled();
    Hint(ctx.source_dirty() ? "Assemble the edited source (Ctrl+B)"
                            : "Re-assemble the source (Ctrl+B)");

    ImGui::SameLine();
    if (running || waiting) {
      if (PrimaryButton("Pause", true)) ctx.Pause();
      Hint("Pause execution (F5)");
    } else {
      ImGui::BeginDisabled(!ctx.can_run());
      const char* label =
          snap.state == SimState::kExited || snap.state == SimState::kFaulted
              ? "Restart"
              : (snap.cpu.instret > 0 ? "Continue" : "Run");
      if (PrimaryButton(label, !needs_assembly)) ctx.RunOrContinue();
      ImGui::EndDisabled();
      Hint("Run until a breakpoint, ebreak, input request, or exit (F5)");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.can_step());
    if (ImGui::Button("Step")) ctx.StepIn();
    ImGui::EndDisabled();
    Hint(ctx.can_step() ? "Execute one instruction (F11)"
                        : "Pause the program to step");

    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.has_program() && !ctx.source_dirty());
    if (ImGui::Button("Reset")) ctx.Reset();
    ImGui::EndDisabled();
    Hint("Reload the program and clear registers and console (Ctrl+R)");

    Separator();

    // Speed.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Speed");
    ImGui::SameLine();
    int speed_index = SpeedIndex(ctx.speed_hz());
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("100 kHz").x * 2.2f);
    if (ImGui::SliderInt(
            "##speed", &speed_index, 0, static_cast<int>(kSpeeds.size()) - 1,
            kSpeeds[static_cast<size_t>(speed_index)].label,
            ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
      ctx.SetSpeedHz(kSpeeds[static_cast<size_t>(speed_index)].hz);
    }
    Hint("Instructions per second while running");
  }
  ImGui::End();
}

void Workspace::RenderStatusBar() {
  Context& ctx = *context_;
  const UiSnapshot& snap = ctx.snapshot();
  const Palette& c = Colors();
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float height =
      ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(spacing::kSm, spacing::kUnit * 0.5f));
  const bool open = ImGui::BeginViewportSideBar("##StatusBar", viewport,
                                                ImGuiDir_Down, height, flags);
  ImGui::PopStyleVar();
  if (open) {
    Pill(ToString(snap.state), StateColor(snap.state));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(c.text_secondary, "%s · %s",
                       snap.config.xlen == Xlen::k32 ? "RV32I" : "RV64I",
                       ToString(snap.config.environment).data());

    if (ctx.has_program()) {
      const int width = snap.cpu.xlen == Xlen::k32 ? 8 : 16;
      Separator();
      ImGui::PushFont(ctx.monospace_font(), 0.0f);
      ImGui::TextUnformatted(
          std::format("pc 0x{:0{}x}", snap.cpu.pc, width).c_str());
      ImGui::PopFont();
      if (auto line = ctx.pc_line()) {
        ImGui::SameLine();
        ImGui::TextColored(c.text_secondary, "line %d", *line + 1);
      }
      Separator();
      ImGui::TextColored(c.text_secondary, "cycle %llu  instret %llu",
                         static_cast<unsigned long long>(snap.cpu.cycle),
                         static_cast<unsigned long long>(snap.cpu.instret));
      if (snap.state == SimState::kRunning) {
        Separator();
        const double hz = snap.effective_hz;
        ImGui::TextColored(c.text_secondary, "%s",
                           hz >= 1e6
                               ? std::format("{:.2f} MIPS", hz / 1e6).c_str()
                               : std::format("{:.0f} IPS", hz).c_str());
      }
    }

    std::string message = snap.message;
    ImVec4 message_color = c.text_secondary;
    if (snap.state == SimState::kFaulted ||
        snap.stop_reason == StopReason::kAssemblyFailed) {
      message_color = c.danger;
    } else if (snap.stop_reason == StopReason::kBreakpoint) {
      message = "Stopped at breakpoint";
    } else if (snap.stop_reason == StopReason::kEbreak) {
      message = "Stopped at ebreak";
    } else if (snap.state == SimState::kWaitingForInput) {
      message = "Type input in the Console and press Enter";
      message_color = c.info;
    }
    if (snap.program_generation == 0) {
      message = "Press Assemble (Ctrl+B) or Run (F5) to start";
      message_color = c.text_secondary;
    } else if (ctx.source_dirty() && snap.state != SimState::kRunning) {
      message = "Source modified · press Ctrl+B to assemble";
      message_color = c.warning;
    }
    if (!message.empty()) {
      Separator();
      ImGui::TextColored(message_color, "%s", message.c_str());
    }
  }
  ImGui::End();
}

void Workspace::RenderDockSpace() {
  Context& ctx = *context_;
  const ImGuiID dockspace_id = ImGui::GetID("rvsim.DockSpace");
  if (!layout_checked_ || ctx.reset_layout_requested) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
    if (ctx.reset_layout_requested || node == nullptr || !node->IsSplitNode()) {
      BuildDefaultLayout(dockspace_id);
    }
    layout_checked_ = true;
    ctx.reset_layout_requested = false;
  }
  ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());
}

void Workspace::BuildDefaultLayout(ImGuiID dockspace_id) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

  ImGuiID left = 0;
  ImGuiID right = 0;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.45f, &left,
                              &right);
  ImGuiID console = 0;
  ImGuiID editor = 0;
  ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.30f, &console, &editor);
  ImGuiID memory = 0;
  ImGuiID right_top = 0;
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, &memory, &right_top);
  ImGuiID registers = 0;
  ImGuiID disassembly = 0;
  ImGui::DockBuilderSplitNode(right_top, ImGuiDir_Right, 0.38f, &registers,
                              &disassembly);

  const auto dock = [&](const char* name, ImGuiID node) {
    if (Window* panel = FindPanel(name)) {
      panel->set_visible(true);
      ImGui::DockBuilderDockWindow(panel->dock_id().c_str(), node);
    }
  };
  dock("Editor", editor);
  dock("Console", console);
  dock("Disassembly", disassembly);
  dock("Registers", registers);
  dock("Memory", memory);
  ImGui::DockBuilderFinish(dockspace_id);

  // Without a central node every split keeps its ratio when the viewport is
  // resized (otherwise one pane absorbs all of the change and can collapse).
  for (const ImGuiID id :
       {left, right, console, editor, memory, right_top, registers,
        disassembly}) {
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(id)) {
      node->SetLocalFlags(node->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
    }
  }
}

void Workspace::RenderAboutDialog() {
  Context& ctx = *context_;
  if (ctx.show_about) {
    ImGui::OpenPopup("About rvsim");
    ctx.show_about = false;
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("About rvsim", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::Text("rvsim %s", CoreVersion().data());
  ImGui::TextColored(Colors().text_secondary,
                     "RISC-V RV32I / RV64I simulator and debugger");
  ImGui::TextColored(Colors().text_secondary, "Dear ImGui %s", IMGUI_VERSION);
  ImGui::Spacing();
  ImGui::SeparatorText("Keyboard shortcuts");
  if (ImGui::BeginTable(
          "##shortcuts", 2,
          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    constexpr std::array<std::pair<const char*, const char*>, 6> kShortcuts = {
        {{"Ctrl+B", "Assemble"},
         {"F5", "Run / Continue / Pause"},
         {"F11", "Step one instruction"},
         {"F9", "Toggle breakpoint at cursor"},
         {"Ctrl+R", "Reset program"},
         {"Ctrl+F", "Find / Replace in editor"}}};
    for (const auto& [keys, action] : kShortcuts) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(keys);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(action);
    }
    ImGui::EndTable();
  }
  ImGui::Spacing();
  if (ImGui::Button("Close", ImVec2(ImGui::GetContentRegionAvail().x, 0)) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace rvsim::ui
