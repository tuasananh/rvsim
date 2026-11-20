#pragma once

#include <memory>
#include <vector>

#include "context.h"
#include "windows/window.h"

namespace rvsim::ui {

// Application shell: main menu bar, execution toolbar, dockspace with the
// default panel layout, status bar, global keyboard shortcuts, and the About
// dialog. Rendered once per frame before the panels.
class Workspace {
 public:
  explicit Workspace(std::shared_ptr<Context> context);

  // Registers a panel for the View menu and the default dock layout.
  void AddPanel(Window* panel) { panels_.push_back(panel); }

  void Render();

 private:
  void HandleShortcuts();
  void RenderMenuBar();
  void RenderToolbar();
  void RenderStatusBar();
  void RenderDockSpace();
  void BuildDefaultLayout(unsigned int dockspace_id);
  void RenderAboutDialog();
  Window* FindPanel(const char* name) const;

  std::shared_ptr<Context> context_;
  std::vector<Window*> panels_;
  bool layout_checked_ = false;
};

}  // namespace rvsim::ui
