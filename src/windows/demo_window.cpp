#include "windows/demo_window.h"
#include "imgui.h"

namespace rvsim {
void DemoWindow::Render() {
  ImGui::ShowDemoWindow();
}
}  // namespace rvsim