#include "windows/memory_window.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string>
#include <string_view>

#include "imgui.h"
#include "simulator/core/registers.h"
#include "ui/design_tokens.h"

namespace rvsim {
namespace {

std::optional<uint64_t> ParseAddress(std::string_view text,
                                     const ProgramImage* program) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  if (text.empty()) return std::nullopt;
  if (program != nullptr) {
    if (auto symbol = program->LookupSymbol(text)) return symbol;
  }
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
  }
  uint64_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::optional<uint8_t> MemoryWindow::Tracked::At(uint64_t addr) const {
  if (addr < address || addr - address >= bytes.size()) return std::nullopt;
  return bytes[addr - address];
}

bool MemoryWindow::Tracked::Mapped(uint64_t addr) const {
  if (addr < address || addr - address >= mapped.size()) return false;
  return mapped[addr - address] != 0;
}

void MemoryWindow::Tracked::Update(const MemorySlice& slice,
                                   uint64_t new_instret) {
  if (new_instret != instret) {
    // Execution happened: highlight bytes that differ from the last view.
    changed.clear();
    if (instret != ~uint64_t{0}) {
      for (size_t i = 0; i < slice.bytes.size(); ++i) {
        const uint64_t addr = slice.address + i;
        const auto old = At(addr);
        if (old && *old != slice.bytes[i]) changed.insert(addr);
      }
    }
    instret = new_instret;
  }
  address = slice.address;
  bytes = slice.bytes;
  mapped = slice.mapped;
}

void MemoryWindow::GoTo(uint64_t address) {
  const uint64_t row_addr = address & ~(kBytesPerRow - 1);
  const uint64_t context_bytes = (kRegionRows / 4) * kBytesPerRow;
  region_base_ = row_addr >= context_bytes ? row_addr - context_bytes : 0;
  pending_scroll_row_ =
      static_cast<int>((row_addr - region_base_) / kBytesPerRow);
  highlight_address_ = address;
  std::snprintf(address_input_.data(), address_input_.size(), "%llx",
                static_cast<unsigned long long>(address));
}

void MemoryWindow::SyncViewports() {
  std::vector<MemoryRequest> requests = {hex_request_, stack_request_};
  if (!visible()) requests.clear();
  if (requests != sent_) {
    sent_ = requests;
    context().controller().SetMemoryViewports(std::move(requests));
  }
}

void MemoryWindow::RenderJumpBar() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ProgramImage* program = snap.program.get();

  ImGui::SetNextItemWidth(ImGui::CalcTextSize("0x0000000000000000").x);
  const bool submitted = ImGui::InputTextWithHint(
      "##address", "address or symbol", address_input_.data(),
      address_input_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Hex address (e.g. 10000000) or symbol name; Enter");
  }
  ImGui::SameLine();
  if (ImGui::Button("Go") || submitted) {
    if (auto address = ParseAddress(address_input_.data(), program)) {
      GoTo(*address);
    }
  }
  const auto jump_button = [&](const char* label, std::optional<uint64_t> to,
                               const char* hint) {
    ImGui::SameLine();
    ImGui::BeginDisabled(!to.has_value());
    if (ImGui::SmallButton(label) && to) GoTo(*to);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("%s", hint);
    }
  };
  const bool loaded = program != nullptr;
  jump_button("PC", loaded ? std::optional(snap.cpu.pc) : std::nullopt,
              "Jump to the program counter");
  jump_button("SP",
              loaded ? std::optional(snap.cpu.ReadReg(reg::kSp)) : std::nullopt,
              "Jump to the stack pointer");
  jump_button("GP",
              loaded ? std::optional(snap.cpu.ReadReg(reg::kGp)) : std::nullopt,
              "Jump to the global pointer");
  const auto segment_start = [&](const char* name) -> std::optional<uint64_t> {
    if (!program) return std::nullopt;
    if (const Segment* s = program->FindSegment(name)) return s->address;
    return std::nullopt;
  };
  jump_button(".text", segment_start(".text"), "Jump to the code segment");
  jump_button(".data", segment_start(".data"), "Jump to the data segment");

  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::CalcTextSize("symbol_name__").x);
  ImGui::BeginDisabled(!program || program->symbols.empty());
  if (ImGui::BeginCombo("##symbol", "Symbol")) {
    if (program) {
      for (const Symbol& symbol : program->symbols) {
        const std::string label =
            std::format("{}  ({:#x})", symbol.name, symbol.address);
        if (ImGui::Selectable(label.c_str())) GoTo(symbol.address);
      }
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::CalcTextSize("DWord").x +
                          ImGui::GetFrameHeight() * 1.5f);
  const char* group_labels[] = {"Byte", "Half", "Word", "DWord"};
  int group_index = grouping_ == 1   ? 0
                    : grouping_ == 2 ? 1
                    : grouping_ == 4 ? 2
                                     : 3;
  if (ImGui::Combo("##grouping", &group_index, group_labels, 4)) {
    grouping_ = 1 << group_index;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Group bytes as little-endian values");
  }
}

void MemoryWindow::RenderHexView() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  const int groups = static_cast<int>(kBytesPerRow) / grouping_;
  const int width = snap.config.xlen == Xlen::k32 ? 8 : 16;

  const ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  const std::string table_id = std::format("##hex{}", grouping_);
  if (ImGui::BeginTable(table_id.c_str(), groups + 2, flags)) {
    const float glyph = ImGui::CalcTextSize("0").x;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_None,
                            glyph * static_cast<float>(width + 1));
    for (int g = 0; g < groups; ++g) {
      const std::string header = std::format("{:X}", g * grouping_);
      ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_None,
                              glyph * static_cast<float>(grouping_ * 2));
    }
    ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(kRegionRows);
    if (pending_scroll_row_ >= 0)
      clipper.IncludeItemByIndex(pending_scroll_row_);
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        if (row == pending_scroll_row_) {
          ImGui::SetScrollHereY(0.0f);
          pending_scroll_row_ = -1;
        }
        const uint64_t row_addr =
            region_base_ + static_cast<uint64_t>(row) * kBytesPerRow;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(c.text_secondary, "%s",
                           std::format("{:0{}x}", row_addr, width).c_str());
        std::string ascii;
        for (int g = 0; g < groups; ++g) {
          ImGui::TableNextColumn();
          const uint64_t cell = row_addr + static_cast<uint64_t>(g * grouping_);
          uint64_t value = 0;
          bool known = true;
          bool mapped = false;
          bool changed = false;
          for (int b = 0; b < grouping_; ++b) {
            const auto byte = hex_.At(cell + static_cast<uint64_t>(b));
            if (!byte) {
              known = false;
              break;
            }
            mapped |= hex_.Mapped(cell + static_cast<uint64_t>(b));
            changed |= hex_.changed.contains(cell + static_cast<uint64_t>(b));
            value |= static_cast<uint64_t>(*byte) << (8 * b);
          }
          const bool highlighted =
              highlight_address_ && *highlight_address_ >= cell &&
              *highlight_address_ < cell + static_cast<uint64_t>(grouping_);
          if (highlighted) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                   ui::U32(c.pc_line_bg));
          }
          if (!known) {
            ImGui::TextColored(
                c.unmapped, "%s",
                std::string(static_cast<size_t>(grouping_) * 2, ' ').c_str());
          } else if (!mapped) {
            ImGui::TextColored(
                c.unmapped, "%s",
                std::string(static_cast<size_t>(grouping_) * 2, '-').c_str());
          } else {
            const std::string text =
                std::format("{:0{}x}", value, grouping_ * 2);
            if (changed) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                     ui::U32(c.changed_bg));
              ImGui::TextColored(c.changed_value, "%s", text.c_str());
            } else if (value == 0) {
              ImGui::TextColored(c.text_disabled, "%s", text.c_str());
            } else {
              ImGui::TextUnformatted(text.c_str());
            }
          }
          for (int b = 0; b < grouping_; ++b) {
            const auto byte = hex_.At(cell + static_cast<uint64_t>(b));
            const char ch = byte ? static_cast<char>(*byte) : ' ';
            ascii.push_back(ch >= 0x20 && ch < 0x7F ? ch : '.');
          }
        }
        ImGui::TableNextColumn();
        ImGui::TextColored(c.text_secondary, "%s", ascii.c_str());
      }
    }
    // Request exactly the rows in view (plus a small margin); computed from
    // the scroll position because the clipper also submits measuring rows.
    const float row_height = std::max(clipper.ItemsHeight, 1.0f);
    const int first =
        std::max(0, static_cast<int>(ImGui::GetScrollY() / row_height) - 2);
    const int count =
        static_cast<int>(ImGui::GetWindowHeight() / row_height) + 4;
    const int last = std::min(kRegionRows, first + count);
    ImGui::EndTable();
    hex_request_ = MemoryRequest{
        region_base_ + static_cast<uint64_t>(first) * kBytesPerRow,
        static_cast<uint32_t>(static_cast<uint64_t>(last - first) *
                              kBytesPerRow)};
  }
  ImGui::PopFont();
}

void MemoryWindow::RenderStackView() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();
  const ui::Palette& c = ui::Colors();
  if (!snap.program) {
    ImGui::TextColored(c.text_secondary, "No program loaded.");
    return;
  }
  const bool rv32 = snap.cpu.xlen == Xlen::k32;
  const uint64_t word = rv32 ? 4 : 8;
  const uint64_t sp = snap.cpu.ReadReg(reg::kSp);
  const uint64_t fp = snap.cpu.ReadReg(reg::kS0);
  const uint64_t top = sp - kStackRowsAbove * word;
  const int width = rv32 ? 8 : 16;

  const ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushFont(ctx.monospace_font(), 0.0f);
  if (ImGui::BeginTable("##stack", 4, flags)) {
    const float glyph = ImGui::CalcTextSize("0").x;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_None, glyph * 9);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_None,
                            glyph * static_cast<float>(width + 1));
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_None,
                            glyph * static_cast<float>(width + 3));
    ImGui::TableSetupColumn("Points to", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (int row = 0; row < kStackRows; ++row) {
      const uint64_t addr = top + static_cast<uint64_t>(row) * word;
      uint64_t value = 0;
      bool known = true;
      bool changed = false;
      for (uint64_t b = 0; b < word; ++b) {
        const auto byte = stack_.At(addr + b);
        if (!byte) {
          known = false;
          break;
        }
        changed |= stack_.changed.contains(addr + b);
        value |= static_cast<uint64_t>(*byte) << (8 * b);
      }
      ImGui::TableNextRow();
      const bool at_sp = addr == sp;
      if (at_sp) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ui::U32(c.pc_line_bg));
      } else if (changed) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ui::U32(c.changed_bg));
      }
      ImGui::TableNextColumn();
      const int64_t offset = static_cast<int64_t>(addr - sp);
      if (at_sp) {
        ImGui::TextColored(c.pc_arrow, "sp ->");
      } else {
        ImGui::TextColored(addr < sp ? c.text_disabled : c.text_secondary, "%s",
                           std::format("sp{}{:#x}", offset < 0 ? "-" : "+",
                                       offset < 0 ? -offset : offset)
                               .c_str());
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(c.text_secondary, "%s",
                         std::format("{:0{}x}", addr, width).c_str());
      if (addr == fp && fp != 0) {
        ImGui::SameLine();
        ImGui::TextColored(c.symbol, "fp");
      }
      ImGui::TableNextColumn();
      if (!known) {
        ImGui::TextUnformatted("");
      } else if (!stack_.Mapped(addr)) {
        ImGui::TextColored(
            c.unmapped, "%s",
            std::string(static_cast<size_t>(width) + 2, '-').c_str());
      } else {
        ImGui::TextColored(changed ? c.changed_value : c.text_primary, "%s",
                           std::format("0x{:0{}x}", value, width).c_str());
      }
      ImGui::TableNextColumn();
      if (known && value != 0) {
        if (const Symbol* symbol = snap.program->NearestSymbol(value)) {
          const Segment* segment = snap.program->SegmentContaining(value);
          if (segment != nullptr) {
            const uint64_t delta = value - symbol->address;
            ImGui::TextColored(
                c.symbol, "%s",
                (delta == 0 ? std::format("<{}>", symbol->name)
                            : std::format("<{}+{:#x}>", symbol->name, delta))
                    .c_str());
          }
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopFont();
  stack_request_ = MemoryRequest{
      top, static_cast<uint32_t>(word * static_cast<uint64_t>(kStackRows))};
}

void MemoryWindow::Render() {
  Context& ctx = context();
  const UiSnapshot& snap = ctx.snapshot();

  if (snap.program_generation != generation_) {
    generation_ = snap.program_generation;
    hex_ = Tracked{};
    stack_ = Tracked{};
    highlight_address_.reset();
    if (snap.program) {
      const Segment* data = snap.program->FindSegment(".data");
      const Segment* text = snap.program->FindSegment(".text");
      GoTo(data ? data->address : text ? text->address : 0);
      highlight_address_.reset();
    }
  }
  if (snap.version != last_version_) {
    last_version_ = snap.version;
    if (!snap.memory.empty()) hex_.Update(snap.memory[0], snap.cpu.instret);
    if (snap.memory.size() > 1) stack_.Update(snap.memory[1], snap.cpu.instret);
  }

  if (visible()) {
    ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(WindowId("Memory").c_str(), visible_ptr())) {
      if (ImGui::BeginTabBar("##memory_tabs")) {
        if (ImGui::BeginTabItem("Memory")) {
          RenderJumpBar();
          RenderHexView();
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stack")) {
          RenderStackView();
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
    }
    ImGui::End();
  }
  SyncViewports();
}

}  // namespace rvsim
