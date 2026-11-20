#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace rvsim {

// Sparse little-endian RAM backed by lazily allocated 4 KiB pages.
//
// Reads from unmapped pages return zero without allocating; writes allocate
// pages on demand up to `max_pages` (writes beyond the budget fail).
class PagedRam {
 public:
  static constexpr uint64_t kPageShift = 12;
  static constexpr uint64_t kPageSize = uint64_t{1} << kPageShift;
  static constexpr uint64_t kPageMask = kPageSize - 1;
  static constexpr size_t kDefaultMaxPages = size_t{1} << 16;  // 256 MiB
  using Page = std::array<uint8_t, kPageSize>;

  explicit PagedRam(size_t max_pages = kDefaultMaxPages);

  PagedRam(const PagedRam&) = delete;
  PagedRam& operator=(const PagedRam&) = delete;
  PagedRam(PagedRam&&) noexcept = default;
  PagedRam& operator=(PagedRam&&) noexcept = default;

  static constexpr uint64_t PageNumber(uint64_t addr) {
    return addr >> kPageShift;
  }
  static constexpr uint64_t PageOffset(uint64_t addr) {
    return addr & kPageMask;
  }

  // Returns the page data or nullptr when the page is not mapped.
  const uint8_t* FindPage(uint64_t page_number) const;
  uint8_t* FindPage(uint64_t page_number);

  // Returns the page data, allocating a zeroed page if needed. Returns nullptr
  // when the page budget is exhausted.
  uint8_t* GetOrCreatePage(uint64_t page_number);

  uint8_t Read8(uint64_t addr) const;
  bool Write8(uint64_t addr, uint8_t value);

  // Bulk accessors spanning arbitrary page boundaries.
  void ReadRegion(uint64_t addr, std::span<uint8_t> out) const;
  bool WriteRegion(uint64_t addr, std::span<const uint8_t> data);
  bool FillRegion(uint64_t addr, uint64_t size, uint8_t value);

  bool IsMapped(uint64_t addr) const;

  // Base addresses of all mapped pages in ascending order.
  std::vector<uint64_t> MappedPageBases() const;

  size_t page_count() const { return pages_.size(); }
  size_t max_pages() const { return max_pages_; }

  void Clear();

 private:
  struct CacheEntry {
    uint64_t page_number = ~uint64_t{0};
    Page* page = nullptr;
  };
  static constexpr size_t kCacheEntries = 4;

  Page* Lookup(uint64_t page_number) const;

  std::unordered_map<uint64_t, std::unique_ptr<Page>> pages_;
  mutable std::array<CacheEntry, kCacheEntries> cache_{};
  size_t max_pages_;
};

}  // namespace rvsim
