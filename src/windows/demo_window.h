#include "windows/window.h"
namespace rvsim {
class DemoWindow : public Window {
 public:
  DemoWindow(std::shared_ptr<Context> context) : Window(std::move(context)) {}
  void Render() override;
};
}  // namespace rvsim