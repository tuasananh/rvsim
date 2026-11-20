#pragma once

#include <optional>

#include "windows/window.h"

namespace rvsim {

// Source editor with RISC-V highlighting, a clickable breakpoint gutter, the
// current-PC arrow, inline assembler diagnostics, and a read-only lock while
// the program runs.
class EditorWindow : public Window {
 public:
  explicit EditorWindow(std::shared_ptr<Context> context);
  void Render() override;

 private:
  void DrawGutter(TextEditor::Decorator& decorator);
  void FollowPc();

  std::optional<int> last_followed_line_;
};

}  // namespace rvsim
