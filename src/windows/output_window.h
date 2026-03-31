#pragma once

#include "windows/window.h"

namespace rvsim {
class OutputWindow : public Window {
 public:
  OutputWindow(std::shared_ptr<Context> context) : Window(std::move(context)) {}
  void Render() override;

 private:
};

}  // namespace rvsim
