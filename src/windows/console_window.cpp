#include "windows/console_window.h"

#include <algorithm>
#include <format>
#include <string>

#include "imgui.h"
#include "ui/design_tokens.h"

namespace rvsim {

void ConsoleWindow::SubmitInput() {
  std::string line(input_.data());
  line.push_back('\n');
  context().controller().SendConsoleInput(std::move(line));
  input_[0] = '\0';
  focus_input_ = true;
}

void ConsoleWindow::RenderConsoleTab() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  const bool waiting = snap.state == SimState::kWaitingForInput;

  if (ImGui::SmallButton("Clear")) ctx.controller().ClearConsole();
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(snap.console.c_str());
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &auto_scroll_);
  if (snap.console_truncated) {
    ImGui::SameLine();
    ImGui::TextColored(c.text_disabled, "(older output truncated)");
  }

  const float input_height =
      ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  if (ImGui::BeginChild("##output", ImVec2(0, -input_height),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    if (snap.console.empty()) {
      ImGui::TextColored(c.text_disabled,
                         ctx.has_program()
                             ? "Program output appears here."
                             : "Assemble and run a program to see its output.");
    } else {
      ImGui::TextUnformatted(snap.console.data(),
                             snap.console.data() + snap.console.size());
    }
    const bool grew = snap.console.size() != last_console_size_;
    last_console_size_ = snap.console.size();
    if (auto_scroll_ && grew) {
      ImGui::SetScrollHereY(1.0f);
    }
  }
  ImGui::EndChild();
  ImGui::PopFont();

  // stdin line.
  const bool can_type = ctx.has_program() && snap.state != SimState::kExited &&
                        snap.state != SimState::kFaulted;
  if (waiting) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(c.info, "stdin >");
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(c.text_disabled, "stdin  ");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_type);
  const float send_width =
      ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - send_width -
                          ImGui::GetStyle().ItemSpacing.x);
  if (focus_input_ && can_type) {
    ImGui::SetKeyboardFocusHere();
    focus_input_ = false;
  }
  const char* hint = waiting ? "The program is waiting for input; press Enter"
                             : "Type input for the program and press Enter";
  if (ImGui::InputTextWithHint("##stdin", hint, input_.data(), input_.size(),
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    SubmitInput();
  }
  ImGui::SameLine();
  if (ImGui::Button("Send")) SubmitInput();
  ImGui::EndDisabled();
}

void ConsoleWindow::RenderDiagnosticsTab() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  if (snap.diagnostics.empty()) {
    if (snap.program_generation == 0) {
      ImGui::TextColored(c.text_secondary,
                         "Assemble (Ctrl+B) to check the source for errors.");
    } else {
      ImGui::TextColored(c.success, "No problems detected.");
    }
    return;
  }
  const ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##diagnostics", 3, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Severity");
    ImGui::TableSetupColumn("Location");
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < snap.diagnostics.size(); ++i) {
      const Diagnostic& d = snap.diagnostics[i];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextColumn();
      const bool error = d.severity == Severity::kError;
      if (ImGui::Selectable("##row", false,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowOverlap)) {
        ctx.JumpToSource(d.line, d.column);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Go to line %d, column %d", d.line, d.column);
      }
      ImGui::SameLine();
      ImGui::TextColored(error ? c.danger : c.warning, "%s",
                         error ? "error" : "warning");
      ImGui::TableNextColumn();
      ImGui::TextColored(c.text_secondary, "%d:%d", d.line, d.column);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(d.message.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void ConsoleWindow::Render() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();

  if (snap.program_generation != generation_) {
    generation_ = snap.program_generation;
    if (snap.stop_reason == StopReason::kAssemblyFailed) {
      select_diagnostics_ = true;
    } else if (snap.program) {
      select_console_ = true;
    }
  }
  if (snap.state != last_state_) {
    if (snap.state == SimState::kWaitingForInput) {
      select_console_ = true;
      focus_input_ = true;
    }
    last_state_ = snap.state;
  }

  if (!visible()) return;
  const size_t errors = static_cast<size_t>(std::ranges::count(
      snap.diagnostics, Severity::kError, &Diagnostic::severity));
  ImGui::SetNextWindowSize(ImVec2(640, 240), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(WindowId("Console").c_str(), visible_ptr())) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("##console_tabs")) {
    if (ImGui::BeginTabItem("Console I/O", nullptr,
                            select_console_ ? ImGuiTabItemFlags_SetSelected
                                            : ImGuiTabItemFlags_None)) {
      RenderConsoleTab();
      ImGui::EndTabItem();
    }
    const std::string diag_label =
        snap.diagnostics.empty() ? std::string("Diagnostics###diagnostics")
                                 : std::format("Diagnostics ({})###diagnostics",
                                               snap.diagnostics.size());
    if (errors > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, ui::Colors().danger);
    }
    const bool diag_open =
        ImGui::BeginTabItem(diag_label.c_str(), nullptr,
                            select_diagnostics_ ? ImGuiTabItemFlags_SetSelected
                                                : ImGuiTabItemFlags_None);
    if (errors > 0) {
      ImGui::PopStyleColor();
    }
    if (diag_open) {
      RenderDiagnosticsTab();
      ImGui::EndTabItem();
    }
    select_console_ = false;
    select_diagnostics_ = false;
    ImGui::EndTabBar();
  }
  ImGui::End();
}

}  // namespace rvsim
