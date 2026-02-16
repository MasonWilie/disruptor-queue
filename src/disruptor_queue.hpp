#include <inttypes.h>

#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#include "bit_utils.hpp"

namespace dq
{

template <typename T, std::size_t CAPACITY>
class disruptor_queue
{
  using sequence_type = int64_t;

  static constexpr sequence_type INITIAL_SEQUENCE = -1;

  static_assert(CAPACITY > 0, "Queue capacity must be positive");
  static_assert(internal::is_power_of_two(CAPACITY),
                "Queue capacity must be a power of two");
  static_assert(std::is_move_assignable<T>::value,
                "Type T must be move assignable");

 public:
  using value_type = T;
  using const_value_type = const T;
  using reference = value_type&;
  using const_reference = const_value_type&;
  using size_type = size_t;

  class reader;
  class writer;

 public:
  disruptor_queue();

  // Reader/Writer creation must be called during setup ONLY
  [[nodiscard]] reader& create_reader();
  [[nodiscard]] writer& create_writer();

  [[nodiscard]] static constexpr size_type capacity() noexcept;

 private:
  static size_type index_from_sequence(sequence_type sequence) noexcept;
  sequence_type get_min_consumer_sequence() const noexcept;

  std::array<value_type, CAPACITY> _buffer{};

  std::array<std::atomic<sequence_type>, CAPACITY> _slot_sequences{};

  std::atomic<sequence_type> _next_sequence{0};

  std::mutex _setup_mutex;
  std::deque<std::unique_ptr<reader>> _readers{};
  std::deque<std::unique_ptr<writer>> _writers{};
};

// ==================== QUEUE ====================

template <typename T, std::size_t CAPACITY>
disruptor_queue<T, CAPACITY>::disruptor_queue()
{
  for (auto& slot_seq : _slot_sequences)
  {
    slot_seq.store(INITIAL_SEQUENCE, std::memory_order_relaxed);
  }
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::create_reader() -> reader&
{
  std::lock_guard<std::mutex> lock(_setup_mutex);
  return *_readers.emplace_back(std::make_unique<reader>(*this));
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::create_writer() -> writer&
{
  std::lock_guard<std::mutex> lock(_setup_mutex);
  return *_writers.emplace_back(std::make_unique<writer>(*this));
}

template <typename T, std::size_t CAPACITY>
constexpr auto disruptor_queue<T, CAPACITY>::capacity() noexcept -> size_type
{
  return CAPACITY;
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::index_from_sequence(
    sequence_type sequence) noexcept -> size_type
{
  return internal::mod_power_of_two<CAPACITY>(sequence);
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::get_min_consumer_sequence() const noexcept
    -> sequence_type
{
  if (_readers.empty())
  {
    return std::numeric_limits<sequence_type>::max();
  }

  sequence_type min_sequence = std::numeric_limits<sequence_type>::max();

  for (const auto& reader_ptr : _readers)
  {
    const sequence_type reader_seq =
        reader_ptr->_consumer_sequence.load(std::memory_order_acquire);
    min_sequence = std::min(min_sequence, reader_seq);
  }

  return min_sequence;
}

// ==================== WRITER ====================
template <typename T, std::size_t CAPACITY>
class alignas(64) disruptor_queue<T, CAPACITY>::writer
{
 public:
  explicit writer(disruptor_queue& queue) noexcept;

  void write(value_type value) noexcept;

  template <typename... Args>
  void write_emplace(Args&&... args);

 private:
  sequence_type claim_sequence() noexcept;
  void commit_sequence(size_type write_index,
                       sequence_type claimed_sequence) noexcept;
  void wait_for_no_wrap(sequence_type claimed_sequence) noexcept;

  disruptor_queue& _queue;
  sequence_type _cached_min_consumer_sequence{INITIAL_SEQUENCE};

  friend class disruptor_queue;
};

template <typename T, std::size_t CAPACITY>
disruptor_queue<T, CAPACITY>::writer::writer(disruptor_queue& queue) noexcept
    : _queue{queue}
{
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::writer::write(value_type value) noexcept
    -> void
{
  const sequence_type claimed_sequence = claim_sequence();

  const size_type write_index = index_from_sequence(claimed_sequence);
  _queue._buffer[write_index] = std::move(value);

  commit_sequence(write_index, claimed_sequence);
}

template <typename T, std::size_t CAPACITY>
template <typename... Args>
auto disruptor_queue<T, CAPACITY>::writer::write_emplace(Args&&... args) -> void
{
  const sequence_type claimed_sequence = claim_sequence();

  const size_type write_index = index_from_sequence(claimed_sequence);

  _queue._buffer[write_index] = value_type{std::forward<Args>(args)...};

  commit_sequence(write_index, claimed_sequence);
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::writer::claim_sequence() noexcept
    -> sequence_type
{
  const sequence_type claimed_sequence =
      _queue._next_sequence.fetch_add(1, std::memory_order_relaxed);

  wait_for_no_wrap(claimed_sequence);

  return claimed_sequence;
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::writer::commit_sequence(
    const size_type write_index,
    const sequence_type claimed_sequence) noexcept -> void
{
  _queue._slot_sequences[write_index].store(claimed_sequence,
                                            std::memory_order_release);
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::writer::wait_for_no_wrap(
    sequence_type claimed_sequence) noexcept -> void
{
  const sequence_type wrap_point =
      claimed_sequence - static_cast<sequence_type>(CAPACITY);

  if (wrap_point <= _cached_min_consumer_sequence)
  {
    return;
  }

  while (wrap_point > _cached_min_consumer_sequence)
  {
    _cached_min_consumer_sequence = _queue.get_min_consumer_sequence();
  }
}

// ==================== READER ====================

template <typename T, std::size_t CAPACITY>
class alignas(64) disruptor_queue<T, CAPACITY>::reader
{
 public:
  explicit reader(disruptor_queue& queue) noexcept;

  value_type read() noexcept;
  void read(reference output) noexcept;

 private:
  sequence_type get_next_read_sequence() noexcept;
  void wait_for_data(std::size_t read_index,
                     sequence_type next_read_sequence) noexcept;
  void update_consumer_sequence(sequence_type next_read_sequence) noexcept;

  disruptor_queue& _queue;
  std::atomic<sequence_type> _consumer_sequence{INITIAL_SEQUENCE};

  friend class disruptor_queue;
};

template <typename T, std::size_t CAPACITY>
disruptor_queue<T, CAPACITY>::reader::reader(disruptor_queue& queue) noexcept
    : _queue(queue)
{
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::reader::read() noexcept -> value_type
{
  const sequence_type next_read_sequence = get_next_read_sequence();
  const size_type read_index = index_from_sequence(next_read_sequence);

  wait_for_data(read_index, next_read_sequence);

  value_type value = _queue._buffer[read_index];

  update_consumer_sequence(next_read_sequence);

  return value;
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::reader::read(reference output) noexcept
    -> void
{
  const sequence_type next_read_sequence = get_next_read_sequence();
  const size_type read_index = index_from_sequence(next_read_sequence);

  wait_for_data(read_index, next_read_sequence);

  output = _queue._buffer[read_index];

  update_consumer_sequence(next_read_sequence);
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::reader::get_next_read_sequence() noexcept
    -> sequence_type
{
  return _consumer_sequence.load(std::memory_order_relaxed) + 1;
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::reader::wait_for_data(
    const std::size_t read_index,
    const sequence_type next_read_sequence) noexcept -> void
{
  while (_queue._slot_sequences[read_index].load(std::memory_order_acquire) !=
         next_read_sequence)
  {
  }
}

template <typename T, std::size_t CAPACITY>
auto disruptor_queue<T, CAPACITY>::reader::update_consumer_sequence(
    const sequence_type next_read_sequence) noexcept -> void
{
  _consumer_sequence.store(next_read_sequence, std::memory_order_release);
}

}  // namespace dq
