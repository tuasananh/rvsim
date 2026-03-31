#pragma once
#include <future>
#include "TextEditor.h"
#include "imgui.h"

namespace rvsim {
using Program = std::vector<std::string>;
class Context {
 public:
  Context();

  const ImVec4& clear_color() const;

  void set_monospace_font(ImFont* font) { monospace_font_ = font; }
  ImFont* monospace_font() const { return monospace_font_; }

  void set_sans_font(ImFont* font) { sans_font_ = font; }
  ImFont* sans_font() const { return sans_font_; }

  TextEditor& text_editor() { return text_editor_; }

  const TextEditor& text_editor() const { return text_editor_; }

  bool is_compiling() const {
    if (compilation_future_.valid()) {
      return compilation_future_.wait_for(std::chrono::seconds(0)) !=
             std::future_status::ready;
    }
    return false;
  }

  void StartCompilation();

 private:
  ImVec4 clear_color_;
  ImFont* monospace_font_;
  ImFont* sans_font_;
  TextEditor text_editor_;
  std::future<Program> compilation_future_;
};
}  // namespace rvsim