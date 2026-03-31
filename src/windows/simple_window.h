#pragma once

#include "windows/window.h"

namespace rvsim {
class SimpleWindow : public Window {
 public:
  SimpleWindow(std::shared_ptr<Context> context) : Window(std::move(context)) {}
  void Render() override;

 private:
};

}  // namespace rvsim