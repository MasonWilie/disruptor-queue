#include "disruptor_queue.hpp"
#include "gtest/gtest.h"

TEST(BasicTest, TestThatItWorks)
{
  dq::disruptor_queue<int, 100> queue;
  EXPECT_EQ(2 + 2, 4);
}