#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <type_traits>
#include <vector>

#include "simulator/core/types.h"
#include "simulator/memory/mmio_device.h"
#include "simulator/memory/paged_ram.h"

namespace rvsim {

static_assert(std::endian::native == std::endian::little,
              "rvsim assumes a little-endian host");

// Routes loads, stores, and instruction fetches to MMIO devices or RAM and
// enforces the configured alignment policy.
class MemoryBus {
 public:
  explicit MemoryBus(size_t max_pages = PagedRam::kDefaultMaxPages);

  PagedRam& ram() { return ram_; }
  const PagedRam& ram() const { return ram_; }

  // When true, misaligned loads/stores raise *AddressMisaligned traps.
  // When false (default), they are performed byte-by-byte.
  void set_strict_alignment(bool strict) { strict_alignment_ = strict; }
  bool strict_alignment() const { return strict_alignment_; }

  // Attaches a non-owning MMIO device covering [base, base + size).
  void AttachDevice(uint64_t base, uint64_t size, MmioDevice* device);
  void DetachAllDevices() { devices_.clear(); }

  template <typename T>
  std::expected<T, Trap> Load(uint64_t addr);

  template <typename T>
  std::expected<void, Trap> Store(uint64_t addr, T value);

  std::expected<uint8_t, Trap> Read8(uint64_t a) { return Load<uint8_t>(a); }
  std::expected<uint16_t, Trap> Read16(uint64_t a) { return Load<uint16_t>(a); }
  std::expected<uint32_t, Trap> Read32(uint64_t a) { return Load<uint32_t>(a); }
  std::expected<uint64_t, Trap> Read64(uint64_t a) { return Load<uint64_t>(a); }
  std::expected<void, Trap> Write8(uint64_t a, uint8_t v) {
    return Store(a, v);
  }
  std::expected<void, Trap> Write16(uint64_t a, uint16_t v) {
    return Store(a, v);
  }
  std::expected<void, Trap> Write32(uint64_t a, uint32_t v) {
    return Store(a, v);
  }
  std::expected<void, Trap> Write64(uint64_t a, uint64_t v) {
    return Store(a, v);
  }

  // Fetches a 32-bit instruction word. Fetching from an unmapped page raises
  // InstructionAccessFault. Alignment (IALIGN) is checked by the caller.
  std::expected<uint32_t, Trap> FetchInstruction32(uint64_t addr) const;

 private:
  struct MmioRegion {
    uint64_t base;
    uint64_t size;
    MmioDevice* device;
  };

  MmioDevice* FindDevice(uint64_t addr, uint64_t& offset) const;

  PagedRam ram_;
  std::vector<MmioRegion> devices_;
  bool strict_alignment_ = false;
};

template <typename T>
std::expected<T, Trap> MemoryBus::Load(uint64_t addr) {
  static_assert(std::is_unsigned_v<T> && sizeof(T) <= 8);
  constexpr uint64_t kSize = sizeof(T);
  const bool aligned = (addr & (kSize - 1)) == 0;
  if (!aligned && strict_alignment_) {
    return std::unexpected(Trap{TrapCause::kLoadAddressMisaligned, addr});
  }
  if (!devices_.empty()) {
    uint64_t offset = 0;
    if (MmioDevice* device = FindDevice(addr, offset)) {
      auto value = device->Read(offset, static_cast<int>(kSize));
      if (!value) {
        return std::unexpected(value.error());
      }
      return static_cast<T>(*value);
    }
  }
  T value{};
  if (aligned) {
    const uint8_t* page = ram_.FindPage(PagedRam::PageNumber(addr));
    if (page != nullptr) {
      std::memcpy(&value, page + PagedRam::PageOffset(addr), kSize);
    }
    return value;
  }
  uint8_t bytes[kSize];
  ram_.ReadRegion(addr, bytes);
  std::memcpy(&value, bytes, kSize);
  return value;
}

template <typename T>
std::expected<void, Trap> MemoryBus::Store(uint64_t addr, T value) {
  static_assert(std::is_unsigned_v<T> && sizeof(T) <= 8);
  constexpr uint64_t kSize = sizeof(T);
  const bool aligned = (addr & (kSize - 1)) == 0;
  if (!aligned && strict_alignment_) {
    return std::unexpected(Trap{TrapCause::kStoreAddressMisaligned, addr});
  }
  if (!devices_.empty()) {
    uint64_t offset = 0;
    if (MmioDevice* device = FindDevice(addr, offset)) {
      return device->Write(offset, static_cast<int>(kSize),
                           static_cast<uint64_t>(value));
    }
  }
  if (aligned) {
    uint8_t* page = ram_.GetOrCreatePage(PagedRam::PageNumber(addr));
    if (page == nullptr) {
      return std::unexpected(Trap{TrapCause::kStoreAccessFault, addr});
    }
    std::memcpy(page + PagedRam::PageOffset(addr), &value, kSize);
    return {};
  }
  uint8_t bytes[kSize];
  std::memcpy(bytes, &value, kSize);
  if (!ram_.WriteRegion(addr, bytes)) {
    return std::unexpected(Trap{TrapCause::kStoreAccessFault, addr});
  }
  return {};
}

}  // namespace rvsim
