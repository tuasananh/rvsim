#include "simulator/loader/program_image.h"

#include <algorithm>

namespace rvsim {

void SourceMap::Add(uint64_t address, int line) {
  by_address_[address] = line;
  by_line_.emplace(line, address);
}

void SourceMap::Clear() {
  by_address_.clear();
  by_line_.clear();
}

std::optional<int> SourceMap::LineForAddress(uint64_t address) const {
  const auto it = by_address_.find(address);
  if (it == by_address_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<uint64_t> SourceMap::AddressForLine(int line) const {
  const auto [begin, end] = by_line_.equal_range(line);
  std::optional<uint64_t> lowest;
  for (auto it = begin; it != end; ++it) {
    if (!lowest || it->second < *lowest) {
      lowest = it->second;
    }
  }
  return lowest;
}

std::vector<uint64_t> SourceMap::AddressesForLine(int line) const {
  std::vector<uint64_t> result;
  const auto [begin, end] = by_line_.equal_range(line);
  for (auto it = begin; it != end; ++it) {
    result.push_back(it->second);
  }
  std::ranges::sort(result);
  return result;
}

const Segment* ProgramImage::FindSegment(std::string_view name) const {
  for (const Segment& segment : segments) {
    if (segment.name == name) {
      return &segment;
    }
  }
  return nullptr;
}

const Segment* ProgramImage::SegmentContaining(uint64_t address) const {
  for (const Segment& segment : segments) {
    if (segment.Contains(address)) {
      return &segment;
    }
  }
  return nullptr;
}

std::optional<uint64_t> ProgramImage::LookupSymbol(
    std::string_view name) const {
  for (const Symbol& symbol : symbols) {
    if (symbol.name == name) {
      return symbol.address;
    }
  }
  return std::nullopt;
}

const Symbol* ProgramImage::NearestSymbol(uint64_t address) const {
  auto it = std::ranges::upper_bound(symbols, address, {}, &Symbol::address);
  if (it == symbols.begin()) {
    return nullptr;
  }
  --it;
  // Prefer the first symbol defined at that address.
  const uint64_t found = it->address;
  while (it != symbols.begin() && std::prev(it)->address == found) {
    --it;
  }
  return &*it;
}

const Symbol* ProgramImage::SymbolAt(uint64_t address) const {
  const Symbol* symbol = NearestSymbol(address);
  return symbol != nullptr && symbol->address == address ? symbol : nullptr;
}

void ProgramImage::FinalizeSymbols() {
  std::ranges::stable_sort(symbols, {}, &Symbol::address);
}

}  // namespace rvsim
