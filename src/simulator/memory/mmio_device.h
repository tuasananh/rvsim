#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

#include "simulator/core/types.h"

namespace rvsim {

// Memory-mapped I/O device attached to the MemoryBus at a fixed base address.
// Offsets are relative to the device base; `size` is 1, 2, 4, or 8 bytes.
class MmioDevice {
 public:
  virtual ~MmioDevice() = default;

  virtual std::string_view name() const = 0;
  virtual std::expected<uint64_t, Trap> Read(uint64_t offset, int size) = 0;
  virtual std::expected<void, Trap> Write(uint64_t offset, int size,
                                          uint64_t value) = 0;
  virtual void Reset() {}
};

}  // namespace rvsim
