#include <memory>

#include "app.h"
#include "windows/console_window.h"
#include "windows/demo_window.h"
#include "windows/editor_window.h"
#include "windows/memory_window.h"
#include "windows/register_window.h"
#include "windows/disassembly_window.h"

int main() {
  using namespace rvsim;  // NOLINT(build/namespaces)

  auto context = std::make_shared<Context>();
  App app(context);
  app.RegisterWindow(std::make_unique<EditorWindow>(context));
  app.RegisterWindow(std::make_unique<DisassemblyWindow>(context));
  app.RegisterWindow(std::make_unique<RegisterWindow>(context));
  app.RegisterWindow(std::make_unique<MemoryWindow>(context));
  app.RegisterWindow(std::make_unique<ConsoleWindow>(context));
  app.RegisterWindow(std::make_unique<DemoWindow>(context));
  app.Loop();
}
