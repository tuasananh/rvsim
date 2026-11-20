#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "windows/window.h"

namespace rvsim {

// Hex + ASCII memory inspector and stack view. Only the visible rows are
// mirrored from the simulator (via controller memory viewports).
class MemoryWindow : public Window {
 public:
  explicit MemoryWindow(std::shared_ptr<Context> context)
      : Window(std::move(context), "Memory") {}
  void Render() override;

 private:
  static constexpr uint64_t kBytesPerRow = 16;
  static constexpr int kRegionRows = 4096;  // 64 KiB scrollable region
  static constexpr int kStackRowsAbove = 8;
  static constexpr int kStackRows = 64;

  // Bytes mirrored from one viewport, used for display and change tracking.
  struct Tracked {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mapped;
    uint64_t instret = ~uint64_t{0};
    std::unordered_set<uint64_t> changed;

    std::optional<uint8_t> At(uint64_t addr) const;
    bool Mapped(uint64_t addr) const;
    void Update(const MemorySlice& slice, uint64_t new_instret);
  };

  void RenderJumpBar();
  void RenderHexView();
  void RenderStackView();
  void GoTo(uint64_t address);
  void SyncViewports();

  uint64_t region_base_ = 0;
  int pending_scroll_row_ = -1;
  std::optional<uint64_t> highlight_address_;
  int grouping_ = 1;  // bytes per cell: 1, 2, 4, 8
  std::array<char, 64> address_input_{};
  uint64_t generation_ = ~uint64_t{0};
  uint64_t last_version_ = 0;

  MemoryRequest hex_request_{};
  MemoryRequest stack_request_{};
  std::vector<MemoryRequest> sent_;
  Tracked hex_;
  Tracked stack_;
};

}  // namespace rvsim
