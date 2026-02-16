#include "disruptor_queue.hpp"
#include "gtest/gtest.h"

namespace dq::test
{

TEST(Disruptor_Queue_Tests, Simple_Type)
{
  disruptor_queue<int, 16> queue;

  auto& writer = queue.create_writer();
  auto& reader = queue.create_reader();

  writer.write(10);
  EXPECT_EQ(reader.read(), 10);

  writer.write(11);
  EXPECT_EQ(reader.read(), 11);

  writer.write(12);
  EXPECT_EQ(reader.read(), 12);

  writer.write(-1);
  EXPECT_EQ(reader.read(), -1);
}

namespace
{

class ConstructableType
{
 public:
  ConstructableType(int a, std::string b, float c)
      : _a(a), _b(std::move(b)), _c(c)
  {
  }

  ConstructableType() = default;

  int get_a() const noexcept { return _a; }
  std::string get_b() const { return _b; }
  float get_c() const noexcept { return _c; }

 private:
  int _a;
  std::string _b;
  float _c;
};

}  // namespace

TEST(Disruptor_Queue_Tests, Constructable_Type)
{
  disruptor_queue<ConstructableType, 16> queue;

  auto& writer = queue.create_writer();
  auto& reader = queue.create_reader();

  ConstructableType value_one{10, "hello", 10.4f};

  writer.write(value_one);

  auto const read_value_one = reader.read();

  EXPECT_EQ(read_value_one.get_a(), 10);
  EXPECT_EQ(read_value_one.get_b(), "hello");
  EXPECT_FLOAT_EQ(read_value_one.get_c(), 10.4f);

  writer.write_emplace(11, "goodbye", 96.8f);

  auto const read_value_two = reader.read();

  EXPECT_EQ(read_value_two.get_a(), 11);
  EXPECT_EQ(read_value_two.get_b(), "goodbye");
  EXPECT_FLOAT_EQ(read_value_two.get_c(), 96.8f);
}

TEST(Disruptor_Queue_Tests, Read_to_Reference)
{
  disruptor_queue<ConstructableType, 16> queue;

  auto& writer = queue.create_writer();
  auto& reader = queue.create_reader();

  ConstructableType value_one{10, "hello", 10.4f};

  writer.write(value_one);

  ConstructableType read_value_one{11, "goodbye", 96.8f};

  reader.read(read_value_one);

  EXPECT_EQ(read_value_one.get_a(), 10);
  EXPECT_EQ(read_value_one.get_b(), "hello");
  EXPECT_FLOAT_EQ(read_value_one.get_c(), 10.4f);
}

}  // namespace dq::test
