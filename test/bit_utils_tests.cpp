#include "bit_utils.hpp"
#include "gtest/gtest.h"

namespace dq::internal::tests
{

namespace
{

class CeilToPowerOfTwoTest
    : public ::testing::TestWithParam<std::tuple<std::size_t, std::size_t>>
{
};

}  // namespace

TEST_P(CeilToPowerOfTwoTest, ceil_to_power_of_two)
{
  auto const [original, expected] = GetParam();

  EXPECT_EQ(expected, ceil_to_power_of_two(original));
}

INSTANTIATE_TEST_CASE_P(
    ceil_to_power_of_two_tests, CeilToPowerOfTwoTest,
    ::testing::Values(std::tuple<std::size_t, std::size_t>(0, 1),
                      std::tuple<std::size_t, std::size_t>(1, 1),
                      std::tuple<std::size_t, std::size_t>(2, 2),
                      std::tuple<std::size_t, std::size_t>(3, 4),
                      std::tuple<std::size_t, std::size_t>(5, 8),
                      std::tuple<std::size_t, std::size_t>(7, 8),
                      std::tuple<std::size_t, std::size_t>(8, 8),
                      std::tuple<std::size_t, std::size_t>(1024, 1024),
                      std::tuple<std::size_t, std::size_t>(1025, 2048),
                      std::tuple<std::size_t, std::size_t>(
                          std::numeric_limits<std::size_t>::max(),
                          std::numeric_limits<std::size_t>::max() -
                              (std::numeric_limits<std::size_t>::max() >> 1))));

class IsPowerOfTwoTest
    : public ::testing::TestWithParam<std::tuple<std::size_t, bool>>
{
};

TEST_P(IsPowerOfTwoTest, is_power_of_two)
{
  auto const [original, expected] = GetParam();

  EXPECT_EQ(expected, is_power_of_two(original));
}

INSTANTIATE_TEST_CASE_P(
    is_power_of_two_tests, IsPowerOfTwoTest,
    ::testing::Values(std::tuple<std::size_t, bool>(0, false),
                      std::tuple<std::size_t, bool>(1, true),
                      std::tuple<std::size_t, bool>(2, true),
                      std::tuple<std::size_t, bool>(3, false),
                      std::tuple<std::size_t, bool>(5, false),
                      std::tuple<std::size_t, bool>(7, false),
                      std::tuple<std::size_t, bool>(8, true),
                      std::tuple<std::size_t, bool>(1024, true),
                      std::tuple<std::size_t, bool>(1025, false),
                      std::tuple<std::size_t, bool>(
                          std::numeric_limits<std::size_t>::max(), false),
                      std::tuple<std::size_t, bool>(
                          std::numeric_limits<std::size_t>::max() -
                              (std::numeric_limits<std::size_t>::max() >> 1),
                          true)));

TEST(ModPowerOfTwoTest, mod_power_of_two_tests)
{
  for (int i = 0; i < 1025; ++i)
  {
    EXPECT_EQ((i % 1), mod_power_of_two<1>(i));
    EXPECT_EQ((i % 2), mod_power_of_two<2>(i));
    EXPECT_EQ((i % 4), mod_power_of_two<4>(i));
    EXPECT_EQ((i % 8), mod_power_of_two<8>(i));
    EXPECT_EQ((i % 16), mod_power_of_two<16>(i));
    EXPECT_EQ((i % 32), mod_power_of_two<32>(i));
    EXPECT_EQ((i % 1024), mod_power_of_two<1024>(i));
  }
}

}  // namespace dq::internal::tests
