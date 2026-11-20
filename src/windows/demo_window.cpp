#include "windows/demo_window.h"

#include "imgui.h"

namespace rvsim {

void DemoWindow::Render() {
  if (visible()) {
    ImGui::ShowDemoWindow(visible_ptr());
  }
}

}  // namespace rvsim
