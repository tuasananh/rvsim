#include "windows/register_window.h"

#include <charconv>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "imgui.h"
#include "simulator/core/registers.h"
#include "ui/design_tokens.h"

namespace rvsim {
namespace {

std::string FormatValue(uint64_t value, Xlen xlen, NumberFormat format) {
  const bool rv32 = xlen == Xlen::k32;
  switch (format) {
    case NumberFormat::kHex:
      return rv32 ? std::format("0x{:08x}", value)
                  : std::format("0x{:016x}", value);
    case NumberFormat::kSigned:
      return rv32 ? std::to_string(static_cast<int32_t>(value))
                  : std::to_string(static_cast<int64_t>(value));
    case NumberFormat::kUnsigned:
      return std::to_string(value);
  }
  return {};
}

// Parses "0x1f", "0b101", "-12", or "42".
std::optional<uint64_t> ParseValue(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  bool negative = false;
  if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  } else if (text.size() > 2 && text[0] == '0' &&
             (text[1] == 'b' || text[1] == 'B')) {
    base = 2;
    text.remove_prefix(2);
  }
  uint64_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return negative ? 0 - value : value;
}

}  // namespace

void RegisterWindow::CommitEdit() {
  Context& ctx = context();
  if (auto value = ParseValue(edit_buffer_.data())) {
    if (editing_ == kPcIndex) {
      ctx.controller().WritePc(*value);
    } else {
      ctx.controller().WriteRegister(static_cast<uint8_t>(editing_), *value);
    }
  }
  editing_ = -1;
}

void RegisterWindow::RenderValueCell(int index, uint64_t value, bool changed) {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  const std::string text =
      FormatValue(value, snap.cpu.xlen, ctx.register_format);

  if (editing_ == index) {
    if (focus_edit_) {
      ImGui::SetKeyboardFocusHere();
      focus_edit_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##edit", edit_buffer_.data(), edit_buffer_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
      CommitEdit();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
               (!ImGui::IsItemActive() && !ImGui::IsItemFocused() &&
                !focus_edit_)) {
      editing_ = -1;
    }
    return;
  }

  const bool editable = ctx.can_edit_machine() && index != 0;
  if (changed) ImGui::PushStyleColor(ImGuiCol_Text, c.changed_value);
  ImGui::Selectable(text.c_str(), false,
                    ImGuiSelectableFlags_AllowDoubleClick |
                        ImGuiSelectableFlags_AllowOverlap);
  if (changed) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "hex      %s\nsigned   %s\nunsigned %s%s",
        FormatValue(value, snap.cpu.xlen, NumberFormat::kHex).c_str(),
        FormatValue(value, snap.cpu.xlen, NumberFormat::kSigned).c_str(),
        FormatValue(value, snap.cpu.xlen, NumberFormat::kUnsigned).c_str(),
        index == 0          ? "\n\nx0 is hard-wired to zero"
        : editable          ? "\n\nDouble-click to edit"
        : ctx.has_program() ? "\n\nPause the program to edit"
                            : "");
  }
  if (editable && ImGui::IsItemHovered() &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    editing_ = index;
    focus_edit_ = true;
    const std::string initial =
        FormatValue(value, snap.cpu.xlen, NumberFormat::kHex);
    std::snprintf(edit_buffer_.data(), edit_buffer_.size(), "%s",
                  initial.c_str());
  }
}

void RegisterWindow::RenderIntegerTab() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();

  int format = static_cast<int>(ctx.register_format);
  ImGui::RadioButton("Hex", &format, static_cast<int>(NumberFormat::kHex));
  ImGui::SameLine();
  ImGui::RadioButton("Signed", &format,
                     static_cast<int>(NumberFormat::kSigned));
  ImGui::SameLine();
  ImGui::RadioButton("Unsigned", &format,
                     static_cast<int>(NumberFormat::kUnsigned));
  ctx.register_format = static_cast<NumberFormat>(format);

  if (editing_ >= 0 && !ctx.can_edit_machine()) {
    editing_ = -1;  // Execution resumed; abandon the edit.
  }

  const ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  if (ImGui::BeginTable("##int_regs", 3, flags)) {
    const float glyph = ImGui::CalcTextSize("0").x;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_None, glyph * 4.0f);
    ImGui::TableSetupColumn("ABI", ImGuiTableColumnFlags_None, glyph * 5.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    // pc first.
    {
      const bool changed = snap.cpu.pc != snap.prev_cpu.pc && ctx.has_program();
      ImGui::TableNextRow();
      ImGui::PushID(kPcIndex);
      if (changed) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ui::U32(c.changed_bg));
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(c.pc_arrow, "pc");
      ImGui::TableNextColumn();
      ImGui::TextColored(c.text_disabled, "-");
      ImGui::TableNextColumn();
      RenderValueCell(kPcIndex, snap.cpu.pc, changed);
      ImGui::PopID();
    }
    for (int i = 0; i < kNumGprs; ++i) {
      const auto index = static_cast<uint8_t>(i);
      const uint64_t value = snap.cpu.ReadReg(index);
      const bool changed = value != snap.prev_cpu.ReadReg(index);
      ImGui::TableNextRow();
      ImGui::PushID(i);
      if (changed) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ui::U32(c.changed_bg));
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(c.text_secondary, "%s",
                         std::string(GprName(index, false)).c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(std::string(GprName(index, true)).c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s",
                          std::string(kGprInfo[index].description).c_str());
      }
      ImGui::TableNextColumn();
      RenderValueCell(i, value, changed);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopFont();
}

void RegisterWindow::Render() {
  if (!visible()) return;
  ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(WindowId("Registers").c_str(), visible_ptr())) {
    ImGui::End();
    return;
  }
  const ui::Palette& c = ui::Colors();
  if (ImGui::BeginTabBar("##register_tabs")) {
    if (ImGui::BeginTabItem("Integer")) {
      RenderIntegerTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Float")) {
      ImGui::TextColored(c.text_secondary, "f0 - f31 and fcsr");
      ImGui::TextWrapped(
          "Floating-point registers become available with the F and D "
          "extensions (Checkpoint 7).");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("CSRs")) {
      ImGui::TextColored(c.text_secondary, "Control and status registers");
      ImGui::TextWrapped(
          "CSRs become available with the Zicsr extension (Checkpoint 5).");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

}  // namespace rvsim
