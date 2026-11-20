#pragma once

#include <array>

#include "windows/window.h"

namespace rvsim {

// Integer register file (plus Float/CSR placeholders) with format toggle,
// change highlighting, and double-click editing while paused.
class RegisterWindow : public Window {
 public:
  explicit RegisterWindow(std::shared_ptr<Context> context)
      : Window(std::move(context), "Registers") {}
  void Render() override;

 private:
  static constexpr int kPcIndex = 32;

  void RenderIntegerTab();
  void RenderValueCell(int index, uint64_t value, bool changed);
  void CommitEdit();

  int editing_ = -1;
  bool focus_edit_ = false;
  std::array<char, 64> edit_buffer_{};
};

}  // namespace rvsim
