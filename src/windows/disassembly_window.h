#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "windows/window.h"

namespace rvsim {

// Virtualized disassembly listing of the loaded program's executable
// segments with symbol headers, breakpoints, the current PC, raw encodings,
// and source line references.
class DisassemblyWindow : public Window {
 public:
  explicit DisassemblyWindow(std::shared_ptr<Context> context)
      : Window(std::move(context), "Disassembly") {}
  void Render() override;

 private:
  struct Row {
    bool is_symbol = false;
    uint64_t address = 0;
    uint32_t word = 0;
    int line = 0;  // 1-based source line, 0 if none.
    std::string text;
  };

  void RebuildRows();
  void RenderInstructionRow(const Row& row, int i);
  int RowForAddress(uint64_t address) const;

  std::vector<Row> rows_;
  uint64_t generation_ = ~uint64_t{0};
  Xlen xlen_ = Xlen::k32;
  uint64_t last_pc_ = ~uint64_t{0};
  int scroll_to_row_ = -1;
};

}  // namespace rvsim
