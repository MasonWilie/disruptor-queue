#include "disruptor_queue.hpp"
#include "gtest/gtest.h"

namespace dq::test
{

TEST(Test, TestThatItWorks)
{
  disruptor_queue<int, 100> queue;

  auto& writer = queue.create_writer();
  auto& reader = queue.create_reader();

  writer.write(10);
  EXPECT_EQ(reader.read(), 10);
}

}  // namespace dq::test
