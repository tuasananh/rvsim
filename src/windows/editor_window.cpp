#include "windows/editor_window.h"

#include <format>

#include "TextEditor.h"
#include "imgui.h"
#include "ui/design_tokens.h"

namespace rvsim {

EditorWindow::EditorWindow(std::shared_ptr<Context> context)
    : Window(std::move(context), "Editor") {
  // Two glyphs wide: breakpoint dot + PC arrow.
  this->context().text_editor().SetLineDecorator(
      -2.0f,
      [this](TextEditor::Decorator& decorator) { DrawGutter(decorator); });
}

void EditorWindow::DrawGutter(TextEditor::Decorator& decorator) {
  Context& ctx = context();
  const ui::Palette& c = ui::Colors();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float h = decorator.height;
  const ImVec2 center(pos.x + h * 0.5f, pos.y + h * 0.5f);
  const float radius = h * 0.28f;
  ImDrawList* draw = ImGui::GetWindowDrawList();

  ImGui::InvisibleButton("##gutter", ImVec2(decorator.width, h));
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    ctx.ToggleBreakpointAtLine(decorator.line);
  }

  const bool has_breakpoint = ctx.HasBreakpointAtLine(decorator.line);
  if (has_breakpoint) {
    // Hollow when the line produced no code in the assembled program.
    if (!ctx.has_program() || ctx.source_dirty() ||
        ctx.LineHasCode(decorator.line)) {
      draw->AddCircleFilled(center, radius, ui::U32(c.breakpoint));
    } else {
      draw->AddCircle(center, radius, ui::U32(c.breakpoint), 0, 1.5f);
    }
  } else if (hovered) {
    draw->AddCircleFilled(center, radius, ui::U32(c.breakpoint_hover));
  }
  if (hovered) {
    ImGui::SetTooltip(has_breakpoint ? "Remove breakpoint (F9)"
                                     : "Add breakpoint (F9)");
  }

  const std::optional<int> pc_line = ctx.pc_line();
  if (pc_line && *pc_line == decorator.line) {
    // Arrow in the second glyph cell plus a translucent line highlight.
    const float x = pos.x + decorator.glyphSize.x * 1.1f;
    const float s = h * 0.30f;
    const ImVec2 mid(x + s, pos.y + h * 0.5f);
    draw->AddTriangleFilled(ImVec2(x - s * 0.4f, mid.y - s),
                            ImVec2(x - s * 0.4f, mid.y + s),
                            ImVec2(mid.x, mid.y), ui::U32(c.pc_arrow));
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
    draw->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(right, pos.y + h),
                        ui::U32(c.pc_line_bg));
  }
}

void EditorWindow::FollowPc() {
  Context& ctx = context();
  const std::optional<int> line = ctx.pc_line();
  if (!ctx.follow_pc || !line || line == last_followed_line_) {
    return;
  }
  // Follow only on discrete stops; avoid fighting the user while running.
  if (ctx.is_running() && ctx.snapshot().speed_hz == 0) {
    return;
  }
  last_followed_line_ = line;
  TextEditor& editor = ctx.text_editor();
  if (*line < editor.GetFirstVisibleLine() + 1 ||
      *line > editor.GetLastVisibleLine() - 1) {
    editor.ScrollToLine(*line, TextEditor::Scroll::alignMiddle);
  }
}

void EditorWindow::Render() {
  if (!visible()) return;
  Context& ctx = context();
  if (ctx.ConsumeEditorFocusRequest()) {
    ImGui::SetNextWindowFocus();
    ctx.text_editor().SetFocus();
  }
  const std::string title =
      WindowId(ctx.source_dirty() ? "Editor *" : "Editor");
  ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title.c_str(), visible_ptr())) {
    ctx.set_editor_focused(false);
    ImGui::End();
    return;
  }
  ctx.set_editor_focused(
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
  FollowPc();

  TextEditor& editor = ctx.text_editor();
  const ui::Palette& c = ui::Colors();

  // Header: lock / modified state and cursor position.
  int line = 0;
  int column = 0;
  editor.GetMainCursor(line, column);
  if (editor.IsReadOnlyEnabled()) {
    ImGui::TextColored(c.info, "Read-only while the program runs");
  } else if (ctx.source_dirty() && ctx.has_program()) {
    ImGui::TextColored(c.warning, "Modified since last assemble");
  } else if (ctx.has_program()) {
    ImGui::TextColored(c.success, "Assembled");
  } else {
    ImGui::TextColored(c.text_secondary, "Not assembled");
  }
  const std::string position =
      std::format("Ln {}, Col {}", line + 1, column + 1);
  ImGui::SameLine(ImGui::GetContentRegionMax().x -
                  ImGui::CalcTextSize(position.c_str()).x);
  ImGui::TextColored(c.text_secondary, "%s", position.c_str());

  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  editor.Render("##source", ImGui::GetContentRegionAvail());
  ImGui::PopFont();
  ImGui::End();
}

}  // namespace rvsim
