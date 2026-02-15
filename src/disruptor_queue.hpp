#include <inttypes.h>

#include <array>
#include <atomic>
#include <limits>
#include <list>
#include <mutex>
#include <thread>

#include "bit_utils.hpp"

namespace dq
{

template <typename T, std::size_t SIZE>
class disruptor_queue
{
  using sequence_type = int64_t;

 public:
  using value_type = T;
  using const_value_type = const T;
  using reference = value_type&;
  using const_reference = const_value_type&;
  using size_type = size_t;

  static constexpr size_type capacity();

  class reader
  {
   public:
    explicit reader(disruptor_queue& queue) noexcept;
    value_type read() noexcept;

   private:
    disruptor_queue& _queue;
    std::atomic<sequence_type> _consumer_sequence;

    friend class disruptor_queue;
  };

  class writer
  {
   public:
    explicit writer(disruptor_queue& queue) noexcept;
    void write(value_type value) noexcept;

   private:
    void wait_for_no_wrap(sequence_type claimed_sequence) noexcept;

    disruptor_queue& _queue;
    sequence_type _min_consumer_sequence;

    friend class disruptor_queue;
  };

 public:
  disruptor_queue() = default;

  reader& create_reader();
  writer& create_writer();

 private:
  static constexpr const std::size_t CAPACITY =
      internal::ceil_to_power_of_two(SIZE);
  friend class reader;

  static std::size_t index_from_sequence(sequence_type sequence) noexcept;
  sequence_type get_min_consumer_sequence() noexcept;

  std::array<value_type, CAPACITY> _buffer;
  std::atomic<sequence_type> _producer_sequence{1};
  std::atomic<sequence_type> _available_sequence{1};

  std::list<reader> _readers;
  std::list<writer> _writers;
};

// ----------- QUEUE -----------------

template <typename T, std::size_t SIZE>
auto disruptor_queue<T, SIZE>::create_reader() -> reader&
{
  return _readers.emplace_back(*this);
}

template <typename T, std::size_t SIZE>
auto disruptor_queue<T, SIZE>::create_writer() -> writer&
{
  return _writers.emplace_back(*this);
}

template <typename T, std::size_t SIZE>
constexpr auto disruptor_queue<T, SIZE>::capacity() -> size_type
{
  return CAPACITY;
}

template <typename T, std::size_t SIZE>
std::size_t disruptor_queue<T, SIZE>::index_from_sequence(
    sequence_type sequence) noexcept
{
  return internal::mod_power_of_two<CAPACITY>(sequence);
}

template <typename T, std::size_t SIZE>
auto disruptor_queue<T, SIZE>::get_min_consumer_sequence() noexcept
    -> sequence_type
{
  sequence_type min_consumer_sequence =
      std::numeric_limits<sequence_type>::max();

  for (auto& reader : _readers)
  {
    min_consumer_sequence =
        std::min(min_consumer_sequence, reader._consumer_sequence.load());
  }

  return min_consumer_sequence;
}

// ---------------- WRITER --------------------

template <typename T, std::size_t SIZE>
disruptor_queue<T, SIZE>::writer::writer(disruptor_queue& queue) noexcept
    : _queue{queue}, _min_consumer_sequence{0}
{
}

template <typename T, std::size_t SIZE>
auto disruptor_queue<T, SIZE>::writer::write(value_type value) noexcept -> void
{
  // Claim a sequence number
  const sequence_type claimed_sequence = _queue._producer_sequence.fetch_add(1);

  // Wait until writing to that sequece number won't cause a wrap
  wait_for_no_wrap(claimed_sequence);

  // Write to that sequence number
  const std::size_t write_index =
      disruptor_queue::index_from_sequence(claimed_sequence);
  _queue._buffer[write_index] = std::move(value);

  const sequence_type previous_sequence = claimed_sequence - 1;

  while (_queue._available_sequence.load() != previous_sequence)
  {
  }

  _queue._available_sequence.store(claimed_sequence);
}

template <typename T, std::size_t SIZE>
void disruptor_queue<T, SIZE>::writer::wait_for_no_wrap(
    sequence_type claimed_sequence) noexcept
{
  const sequence_type wrap_point =
      claimed_sequence -
      static_cast<sequence_type>(disruptor_queue::capacity());

  while (wrap_point > _min_consumer_sequence)
  {
    _min_consumer_sequence = _queue.get_min_consumer_sequence();
  }
}

// --------------- READER -----------------

template <typename T, std::size_t SIZE>
disruptor_queue<T, SIZE>::reader::reader(disruptor_queue& queue) noexcept
    : _queue(queue)
{
}

template <typename T, std::size_t SIZE>
auto disruptor_queue<T, SIZE>::reader::read() noexcept -> value_type
{
  // Get the next sequence that we want to read
  const sequence_type next_read_sequence = _consumer_sequence.load() + 1;
  const int read_index = index_from_sequence(next_read_sequence);

  // Wait until that sequence is actually associated with a value
  while (next_read_sequence > _queue._available_sequence.load())
  {
  }

  // Read from that sequence
  value_type value = _queue._buffer[read_index];

  // Broadcast that we've read from the sequence
  _consumer_sequence.store(next_read_sequence);

  return value;
}

}  // namespace dq
