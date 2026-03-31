#include <memory>
#include "app.h"
#include "windows/demo_window.h"
#include "windows/simple_window.h"

int main() {
  using namespace rvsim;

  auto context = std::make_shared<Context>();
  auto simple_window = std::make_unique<SimpleWindow>(context);
  auto demo_window = std::make_unique<DemoWindow>(context);
  App app(std::move(context));
  app.RegisterWindow(std::move(simple_window));
  app.RegisterWindow(std::move(demo_window));
  app.Loop();
}
