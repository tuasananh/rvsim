#include "context.h"
#include <future>
#include <thread>
#include "TextEditor.h"
#include "imgui.h"
#include "ranges"

namespace rvsim {
Context::Context() : clear_color_(0.0f, 0.0f, 0.0313f, 1.0f) {
  text_editor_.SetLanguage(TextEditor::Language::Python());
}

const ImVec4& Context::clear_color() const {
  return clear_color_;
}

void Context::StartCompilation() {
  compilation_future_ =
      std::async(std::launch::async, [this]() -> std::vector<std::string> {
        // Simulate a long compilation process
        std::this_thread::sleep_for(std::chrono::seconds(3));
        auto program = text_editor_.GetText() | std::views::split('\n') |
                       std::views::transform([](auto&& line) {
                         return std::string(line.begin(), line.end());
                       });
        return std::vector(program.begin(), program.end());
      });
}
}  // namespace rvsim