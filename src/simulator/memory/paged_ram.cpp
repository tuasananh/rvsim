#include "simulator/memory/paged_ram.h"

#include <algorithm>
#include <cstring>

namespace rvsim {

PagedRam::PagedRam(size_t max_pages) : max_pages_(max_pages) {}

PagedRam::Page* PagedRam::Lookup(uint64_t page_number) const {
  CacheEntry& entry = cache_[page_number % kCacheEntries];
  if (entry.page_number == page_number) {
    return entry.page;
  }
  const auto it = pages_.find(page_number);
  if (it == pages_.end()) {
    return nullptr;
  }
  entry.page_number = page_number;
  entry.page = it->second.get();
  return entry.page;
}

const uint8_t* PagedRam::FindPage(uint64_t page_number) const {
  const Page* page = Lookup(page_number);
  return page != nullptr ? page->data() : nullptr;
}

uint8_t* PagedRam::FindPage(uint64_t page_number) {
  Page* page = Lookup(page_number);
  return page != nullptr ? page->data() : nullptr;
}

uint8_t* PagedRam::GetOrCreatePage(uint64_t page_number) {
  if (Page* page = Lookup(page_number)) {
    return page->data();
  }
  if (pages_.size() >= max_pages_) {
    return nullptr;
  }
  auto page = std::make_unique<Page>();
  page->fill(0);
  Page* raw = page.get();
  pages_.emplace(page_number, std::move(page));
  CacheEntry& entry = cache_[page_number % kCacheEntries];
  entry.page_number = page_number;
  entry.page = raw;
  return raw->data();
}

uint8_t PagedRam::Read8(uint64_t addr) const {
  const uint8_t* page = FindPage(PageNumber(addr));
  return page != nullptr ? page[PageOffset(addr)] : 0;
}

bool PagedRam::Write8(uint64_t addr, uint8_t value) {
  uint8_t* page = GetOrCreatePage(PageNumber(addr));
  if (page == nullptr) {
    return false;
  }
  page[PageOffset(addr)] = value;
  return true;
}

void PagedRam::ReadRegion(uint64_t addr, std::span<uint8_t> out) const {
  size_t done = 0;
  while (done < out.size()) {
    const uint64_t cur = addr + done;
    const uint64_t offset = PageOffset(cur);
    const size_t chunk =
        std::min<size_t>(out.size() - done, kPageSize - offset);
    if (const uint8_t* page = FindPage(PageNumber(cur))) {
      std::memcpy(out.data() + done, page + offset, chunk);
    } else {
      std::memset(out.data() + done, 0, chunk);
    }
    done += chunk;
  }
}

bool PagedRam::WriteRegion(uint64_t addr, std::span<const uint8_t> data) {
  size_t done = 0;
  while (done < data.size()) {
    const uint64_t cur = addr + done;
    const uint64_t offset = PageOffset(cur);
    const size_t chunk =
        std::min<size_t>(data.size() - done, kPageSize - offset);
    uint8_t* page = GetOrCreatePage(PageNumber(cur));
    if (page == nullptr) {
      return false;
    }
    std::memcpy(page + offset, data.data() + done, chunk);
    done += chunk;
  }
  return true;
}

bool PagedRam::FillRegion(uint64_t addr, uint64_t size, uint8_t value) {
  uint64_t done = 0;
  while (done < size) {
    const uint64_t cur = addr + done;
    const uint64_t offset = PageOffset(cur);
    const uint64_t chunk = std::min<uint64_t>(size - done, kPageSize - offset);
    uint8_t* page = GetOrCreatePage(PageNumber(cur));
    if (page == nullptr) {
      return false;
    }
    std::memset(page + offset, value, static_cast<size_t>(chunk));
    done += chunk;
  }
  return true;
}

bool PagedRam::IsMapped(uint64_t addr) const {
  return FindPage(PageNumber(addr)) != nullptr;
}

std::vector<uint64_t> PagedRam::MappedPageBases() const {
  std::vector<uint64_t> bases;
  bases.reserve(pages_.size());
  for (const auto& [number, page] : pages_) {
    bases.push_back(number << kPageShift);
  }
  std::ranges::sort(bases);
  return bases;
}

void PagedRam::Clear() {
  pages_.clear();
  cache_.fill(CacheEntry{});
}

}  // namespace rvsim
