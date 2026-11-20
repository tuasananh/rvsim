#include <gtest/gtest.h>

#include "simulator/core/types.h"
#include "simulator/simulator.h"

namespace rvsim {
namespace {

TEST(SmokeTest, CoreLibraryLinks) { EXPECT_FALSE(CoreVersion().empty()); }

TEST(SmokeTest, IsaExtensionsMask) {
  IsaExtensions ext;
  EXPECT_FALSE(ext.Has(Extension::kM));
  ext.Enable(Extension::kM).Enable(Extension::kC);
  EXPECT_TRUE(ext.Has(Extension::kM));
  EXPECT_TRUE(ext.Has(Extension::kC));
  EXPECT_FALSE(ext.Has(Extension::kA));
}

}  // namespace
}  // namespace rvsim
