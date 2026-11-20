#include "simulator/memory/memory_bus.h"

namespace rvsim {

MemoryBus::MemoryBus(size_t max_pages) : ram_(max_pages) {}

void MemoryBus::AttachDevice(uint64_t base, uint64_t size, MmioDevice* device) {
  devices_.push_back(MmioRegion{base, size, device});
}

MmioDevice* MemoryBus::FindDevice(uint64_t addr, uint64_t& offset) const {
  for (const MmioRegion& region : devices_) {
    if (addr >= region.base && addr - region.base < region.size) {
      offset = addr - region.base;
      return region.device;
    }
  }
  return nullptr;
}

std::expected<uint32_t, Trap> MemoryBus::FetchInstruction32(
    uint64_t addr) const {
  if (!ram_.IsMapped(addr) || !ram_.IsMapped(addr + 3)) {
    return std::unexpected(Trap{TrapCause::kInstructionAccessFault, addr});
  }
  uint8_t bytes[4];
  ram_.ReadRegion(addr, bytes);
  uint32_t word = 0;
  std::memcpy(&word, bytes, sizeof(word));
  return word;
}

}  // namespace rvsim
