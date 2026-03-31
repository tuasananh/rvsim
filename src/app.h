#pragma once
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include "context.h"
#include "windows/window.h"

namespace rvsim {
class App {
 public:
  App(std::shared_ptr<Context> context);
  ~App();

  void Loop();

  void RegisterWindow(std::unique_ptr<Window>);

 private:
  std::shared_ptr<Context> context_;
  std::vector<std::unique_ptr<Window>> windows_;
  GLFWwindow* window_;
};
}  // namespace rvsim