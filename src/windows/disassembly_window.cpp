#include "windows/disassembly_window.h"

#include <algorithm>
#include <format>

#include "imgui.h"
#include "simulator/core/decoder.h"
#include "simulator/core/disassembler.h"
#include "ui/design_tokens.h"

namespace rvsim {

void DisassemblyWindow::RebuildRows() {
  rows_.clear();
  const UiSnapshot& snap = context().snapshot();
  generation_ = snap.program_generation;
  xlen_ = snap.config.xlen;
  if (!snap.program) return;
  const ProgramImage& program = *snap.program;
  const std::vector<Symbol>& symbols = program.symbols;  // Sorted.
  for (const Segment& segment : program.segments) {
    if (!segment.executable) continue;
    auto symbol = std::ranges::lower_bound(symbols, segment.address, {},
                                           &Symbol::address);
    for (size_t offset = 0; offset + 4 <= segment.data.size(); offset += 4) {
      const uint64_t address = segment.address + offset;
      // Show every label defined at this address as a header row.
      while (symbol != symbols.end() && symbol->address < address) ++symbol;
      for (; symbol != symbols.end() && symbol->address == address; ++symbol) {
        rows_.push_back(Row{.is_symbol = true,
                            .address = address,
                            .text = std::format("<{}>:", symbol->name)});
      }
      uint32_t word = 0;
      for (int b = 0; b < 4; ++b) {
        word |=
            static_cast<uint32_t>(segment.data[offset + static_cast<size_t>(b)])
            << (8 * b);
      }
      const DecodedInst inst = Decode(word, program.xlen);
      rows_.push_back(
          Row{.address = address,
              .word = word,
              .line = program.source_map.LineForAddress(address).value_or(0),
              .text = Disassemble(inst, address)});
    }
  }
}

int DisassemblyWindow::RowForAddress(uint64_t address) const {
  const auto it = std::ranges::find_if(rows_, [&](const Row& row) {
    return !row.is_symbol && row.address == address;
  });
  return it == rows_.end() ? -1 : static_cast<int>(it - rows_.begin());
}

void DisassemblyWindow::RenderInstructionRow(const Row& row, int i) {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  const int width = xlen_ == Xlen::k32 ? 8 : 16;
  const bool is_pc = row.address == snap.cpu.pc;
  const bool has_bp = ctx.HasBreakpointAtAddress(row.address);
  if (is_pc) {
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ui::U32(c.pc_line_bg));
  }

  // Breakpoint + PC gutter.
  ImGui::TableSetColumnIndex(0);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetTextLineHeight();
  const float w = ImGui::GetContentRegionAvail().x;
  ImGui::InvisibleButton("##bp", ImVec2(std::max(w, 1.0f), h));
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked() && row.line > 0) {
    ctx.ToggleBreakpointAtAddress(row.address);
  }
  if (hovered && row.line > 0) {
    ImGui::SetTooltip(has_bp ? "Remove breakpoint" : "Add breakpoint");
  }
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 dot(pos.x + h * 0.45f, pos.y + h * 0.5f);
  if (has_bp) {
    draw->AddCircleFilled(dot, h * 0.27f, ui::U32(c.breakpoint));
  } else if (hovered && row.line > 0) {
    draw->AddCircleFilled(dot, h * 0.27f, ui::U32(c.breakpoint_hover));
  }
  if (is_pc) {
    const float x = pos.x + h * 1.05f;
    const float s = h * 0.28f;
    draw->AddTriangleFilled(ImVec2(x, dot.y - s), ImVec2(x, dot.y + s),
                            ImVec2(x + s * 1.4f, dot.y), ui::U32(c.pc_arrow));
  }

  ImGui::TableSetColumnIndex(1);
  ImGui::TextColored(is_pc ? c.pc_arrow : c.text_secondary, "%s",
                     std::format("{:0{}x}", row.address, width).c_str());
  ImGui::TableSetColumnIndex(2);
  ImGui::TextColored(c.text_disabled, "%08x", row.word);
  ImGui::TableSetColumnIndex(3);
  ImGui::TextUnformatted(row.text.c_str());
  ImGui::TableSetColumnIndex(4);
  if (row.line > 0) {
    if (ImGui::Selectable(std::format("{}", row.line).c_str(), false,
                          ImGuiSelectableFlags_None)) {
      ctx.JumpToSource(row.line, 1);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to source line");
  }
  if (scroll_to_row_ == i) {
    ImGui::SetScrollHereY(0.4f);
    scroll_to_row_ = -1;
  }
}

void DisassemblyWindow::Render() {
  if (!visible()) return;
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  if (snap.program_generation != generation_) {
    RebuildRows();
    last_pc_ = ~uint64_t{0};
  }

  ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(WindowId("Disassembly").c_str(), visible_ptr())) {
    ImGui::End();
    return;
  }

  if (!snap.program) {
    ImGui::TextColored(c.text_secondary, "No program loaded.");
    ImGui::TextWrapped(
        "Write assembly in the Editor and press Assemble (Ctrl+B) to see "
        "the machine code here.");
    ImGui::End();
    return;
  }

  // Toolbar.
  ImGui::Checkbox("Follow PC", &ctx.follow_pc);
  ImGui::SameLine();
  if (ImGui::Button("Go to PC")) {
    scroll_to_row_ = RowForAddress(snap.cpu.pc);
  }
  ImGui::SameLine();
  ImGui::TextColored(c.text_secondary, "%zu symbols",
                     snap.program->symbols.size());

  if (snap.cpu.pc != last_pc_) {
    last_pc_ = snap.cpu.pc;
    if (ctx.follow_pc &&
        !(snap.state == SimState::kRunning && snap.speed_hz == 0)) {
      scroll_to_row_ = RowForAddress(snap.cpu.pc);
    }
  }

  const ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  if (ImGui::BeginTable("##disasm", 5, flags)) {
    const float glyph = ImGui::CalcTextSize("0").x;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(
        "##bp", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide,
        glyph * 2.5f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_None,
                            glyph * (xlen_ == Xlen::k32 ? 8.5f : 16.5f));
    ImGui::TableSetupColumn("Encoding", ImGuiTableColumnFlags_None,
                            glyph * 8.5f);
    ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_None, glyph * 4.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()));
    if (scroll_to_row_ >= 0) {
      clipper.IncludeItemByIndex(scroll_to_row_);
    }
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const Row& row = rows_[static_cast<size_t>(i)];
        ImGui::TableNextRow();
        if (row.is_symbol) {
          ImGui::TableSetColumnIndex(3);
          ImGui::TextColored(c.symbol, "%s", row.text.c_str());
        } else {
          ImGui::PushID(i);
          RenderInstructionRow(row, i);
          ImGui::PopID();
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopFont();
  ImGui::End();
}

}  // namespace rvsim
