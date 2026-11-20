#pragma once

#include <array>
#include <cstdint>

#include "windows/window.h"

namespace rvsim {

// Console I/O (program stdout/stderr + stdin line input) and assembler
// Diagnostics with click-to-jump into the editor.
class ConsoleWindow : public Window {
 public:
  explicit ConsoleWindow(std::shared_ptr<Context> context)
      : Window(std::move(context), "Console") {}
  void Render() override;

 private:
  void RenderConsoleTab();
  void RenderDiagnosticsTab();
  void SubmitInput();

  std::array<char, 512> input_{};
  bool auto_scroll_ = true;
  size_t last_console_size_ = 0;
  uint64_t generation_ = ~uint64_t{0};
  bool select_diagnostics_ = false;
  bool select_console_ = false;
  bool focus_input_ = false;
  SimState last_state_ = SimState::kNoProgram;
};

}  // namespace rvsim
