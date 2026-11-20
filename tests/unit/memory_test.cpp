#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "simulator/memory/memory_bus.h"
#include "simulator/memory/paged_ram.h"

namespace rvsim {
namespace {

TEST(PagedRamTest, UnmappedReadsAreZeroAndDoNotAllocate) {
  PagedRam ram;
  EXPECT_EQ(ram.Read8(0x1234), 0);
  EXPECT_FALSE(ram.IsMapped(0x1234));
  EXPECT_EQ(ram.page_count(), 0u);
}

TEST(PagedRamTest, WriteAllocatesSinglePage) {
  PagedRam ram;
  ASSERT_TRUE(ram.Write8(0x80000123, 0xAB));
  EXPECT_EQ(ram.Read8(0x80000123), 0xAB);
  EXPECT_EQ(ram.Read8(0x80000124), 0x00);
  EXPECT_EQ(ram.page_count(), 1u);
  EXPECT_TRUE(ram.IsMapped(0x80000000));
  EXPECT_FALSE(ram.IsMapped(0x80001000));
}

TEST(PagedRamTest, RegionsSpanPageBoundaries) {
  PagedRam ram;
  std::vector<uint8_t> data(10000);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i * 7);
  }
  constexpr uint64_t kBase = 0x0000003ffffff000ULL - 100;
  ASSERT_TRUE(ram.WriteRegion(kBase, data));
  std::vector<uint8_t> out(data.size());
  ram.ReadRegion(kBase, out);
  EXPECT_EQ(out, data);
  EXPECT_EQ(ram.page_count(), 4u);
}

TEST(PagedRamTest, MappedPageBasesAreSorted) {
  PagedRam ram;
  ASSERT_TRUE(ram.Write8(0x7ffffff0, 1));
  ASSERT_TRUE(ram.Write8(0x00010000, 1));
  ASSERT_TRUE(ram.Write8(0x10000000, 1));
  const std::vector<uint64_t> expected = {0x00010000, 0x10000000, 0x7ffff000};
  EXPECT_EQ(ram.MappedPageBases(), expected);
}

TEST(PagedRamTest, PageBudgetIsEnforced) {
  PagedRam ram(2);
  EXPECT_TRUE(ram.Write8(0x0000, 1));
  EXPECT_TRUE(ram.Write8(0x1000, 1));
  EXPECT_FALSE(ram.Write8(0x2000, 1));
  EXPECT_TRUE(ram.Write8(0x1FFF, 2));
}

TEST(PagedRamTest, CacheStaysCoherentAcrossManyPages) {
  PagedRam ram;
  for (uint64_t i = 0; i < 64; ++i) {
    ASSERT_TRUE(ram.Write8(i * PagedRam::kPageSize, static_cast<uint8_t>(i)));
  }
  for (uint64_t i = 0; i < 64; ++i) {
    EXPECT_EQ(ram.Read8(i * PagedRam::kPageSize), i);
  }
  ram.Clear();
  EXPECT_EQ(ram.page_count(), 0u);
  EXPECT_EQ(ram.Read8(0), 0);
}

TEST(MemoryBusTest, LittleEndianTypedAccess) {
  MemoryBus bus;
  ASSERT_TRUE(bus.Write32(0x1000, 0x11223344));
  EXPECT_EQ(*bus.Read8(0x1000), 0x44);
  EXPECT_EQ(*bus.Read8(0x1003), 0x11);
  EXPECT_EQ(*bus.Read16(0x1002), 0x1122);
  ASSERT_TRUE(bus.Write64(0x2000, 0x8877665544332211ULL));
  EXPECT_EQ(*bus.Read32(0x2004), 0x88776655u);
  EXPECT_EQ(*bus.Read64(0x2000), 0x8877665544332211ULL);
}

TEST(MemoryBusTest, MisalignedAccessAllowedByDefault) {
  MemoryBus bus;
  constexpr uint64_t kAddr = 0x1FFE;  // Crosses a page boundary.
  ASSERT_TRUE(bus.Write32(kAddr, 0xDEADBEEF));
  EXPECT_EQ(*bus.Read32(kAddr), 0xDEADBEEFu);
  EXPECT_EQ(*bus.Read8(0x2000), 0xAD);
}

TEST(MemoryBusTest, StrictAlignmentRaisesTraps) {
  MemoryBus bus;
  bus.set_strict_alignment(true);
  auto load = bus.Read32(0x1002);
  ASSERT_FALSE(load);
  EXPECT_EQ(load.error(), (Trap{TrapCause::kLoadAddressMisaligned, 0x1002}));
  auto store = bus.Write16(0x1001, 1);
  ASSERT_FALSE(store);
  EXPECT_EQ(store.error(), (Trap{TrapCause::kStoreAddressMisaligned, 0x1001}));
  EXPECT_TRUE(bus.Write8(0x1001, 1));
}

TEST(MemoryBusTest, StoreBeyondBudgetFaults) {
  MemoryBus bus(1);
  ASSERT_TRUE(bus.Write32(0x0, 1));
  auto store = bus.Write32(0x5000, 1);
  ASSERT_FALSE(store);
  EXPECT_EQ(store.error().cause, TrapCause::kStoreAccessFault);
}

TEST(MemoryBusTest, FetchFromUnmappedFaults) {
  MemoryBus bus;
  auto fetch = bus.FetchInstruction32(0x400000);
  ASSERT_FALSE(fetch);
  EXPECT_EQ(fetch.error(),
            (Trap{TrapCause::kInstructionAccessFault, 0x400000}));
  ASSERT_TRUE(bus.Write32(0x400000, 0x00100513));
  EXPECT_EQ(*bus.FetchInstruction32(0x400000), 0x00100513u);
}

class FakeDevice final : public MmioDevice {
 public:
  std::string_view name() const override { return "fake"; }
  std::expected<uint64_t, Trap> Read(uint64_t offset, int size) override {
    last_offset = offset;
    last_size = size;
    return 0xA5A5A5A5A5A5A5A5ULL;
  }
  std::expected<void, Trap> Write(uint64_t offset, int size,
                                  uint64_t value) override {
    last_offset = offset;
    last_size = size;
    last_value = value;
    return {};
  }
  uint64_t last_offset = 0;
  int last_size = 0;
  uint64_t last_value = 0;
};

TEST(MemoryBusTest, MmioDevicesInterceptAccesses) {
  MemoryBus bus;
  FakeDevice device;
  bus.AttachDevice(0x10000000, 0x100, &device);
  ASSERT_TRUE(bus.Write8(0x10000005, 0x42));
  EXPECT_EQ(device.last_offset, 5u);
  EXPECT_EQ(device.last_size, 1);
  EXPECT_EQ(device.last_value, 0x42u);
  EXPECT_EQ(*bus.Read16(0x10000010), 0xA5A5);
  EXPECT_EQ(device.last_size, 2);
  EXPECT_FALSE(bus.ram().IsMapped(0x10000000));
}

}  // namespace
}  // namespace rvsim
