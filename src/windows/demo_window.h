#pragma once

#include "windows/window.h"

namespace rvsim {

// Dear ImGui demo, hidden by default (Help -> Dear ImGui Demo).
class DemoWindow : public Window {
 public:
  explicit DemoWindow(std::shared_ptr<Context> context)
      : Window(std::move(context), "ImGuiDemo", /*visible=*/false) {}
  void Render() override;
};

}  // namespace rvsim
