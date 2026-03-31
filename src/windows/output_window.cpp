#include "windows/output_window.h"

namespace rvsim {
void OutputWindow::Render() {
  ImGui::Begin("Output");
  ImGui::TextWrapped("This is the output window.");
  ImGui::End();
}
}  // namespace rvsim