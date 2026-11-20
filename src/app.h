#pragma once
#include <GLFW/glfw3.h>

#include <memory>
#include <vector>

#include "context.h"
#include "ui/workspace.h"
#include "windows/window.h"

namespace rvsim {
class App {
 public:
  App(std::shared_ptr<Context> context);
  ~App();

  void Loop();

  // Takes ownership of a panel, lists it in the workspace, and returns it.
  Window* RegisterWindow(std::unique_ptr<Window> window);

 private:
  std::shared_ptr<Context> context_;
  std::vector<std::unique_ptr<Window>> windows_;
  std::unique_ptr<ui::Workspace> workspace_;
  GLFWwindow* window_;
};
}  // namespace rvsim