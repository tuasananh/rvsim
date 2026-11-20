#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "context.h"

namespace rvsim {

// Base class for dockable IDE panels. `name()` is the stable ImGui window
// name used for docking and the View menu.
class Window {
 public:
  Window(std::shared_ptr<Context> context, const char* name,
         bool visible = true)
      : context_(std::move(context)), name_(name), visible_(visible) {}
  virtual ~Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  virtual void Render() = 0;

  const char* name() const { return name_; }
  // Stable ImGui window ID ("###name") for docking, independent of title.
  std::string dock_id() const { return std::format("###{}", name_); }
  bool visible() const { return visible_; }
  void set_visible(bool visible) { visible_ = visible; }
  bool* visible_ptr() { return &visible_; }

 protected:
  // "<title>###<name>": lets a panel change its visible title (e.g. counts
  // or a dirty marker) without losing its docking position.
  std::string WindowId(std::string_view title) const {
    return std::format("{}###{}", title, name_);
  }

  Context& context() { return *context_; }
  const Context& context() const { return *context_; }

 private:
  std::shared_ptr<Context> context_;
  const char* name_;
  bool visible_;
};

}  // namespace rvsim
