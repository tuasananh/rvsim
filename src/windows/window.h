#pragma once

#include <memory>
#include "context.h"

namespace rvsim {
class Window {
 public:
  Window(std::shared_ptr<Context> context) : context_(std::move(context)) {}
  virtual ~Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  virtual void Render() = 0;

 protected:
  Context& context() { return *context_; }
  const Context& context() const { return *context_; }

 private:
  std::shared_ptr<Context> context_;
};
}  // namespace rvsim