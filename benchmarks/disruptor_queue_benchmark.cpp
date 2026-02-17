#include <benchmark/benchmark.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "disruptor_queue.hpp"

namespace
{

// Simple payload types for benchmarking
struct SmallPayload
{
  int64_t value;
};

struct MediumPayload
{
  int64_t values[8];  // 64 bytes
};

struct LargePayload
{
  int64_t values[64];  // 512 bytes
};

// Single producer, single consumer throughput benchmark
// Measures steady-state throughput with queue reused across iterations
template <typename T, std::size_t CAPACITY>
void BM_SPSC_Throughput(benchmark::State& state)
{
  const int64_t items_per_iteration = state.range(0);

  for (auto _ : state)
  {
    dq::disruptor_queue<T, CAPACITY> queue;
    auto& writer = queue.create_writer();
    auto& reader = queue.create_reader();

    std::thread consumer([&]() {
      for (int64_t i = 0; i < items_per_iteration; ++i)
      {
        benchmark::DoNotOptimize(reader.read());
      }
    });

    for (int64_t i = 0; i < items_per_iteration; ++i)
    {
      writer.write(T{});
    }

    consumer.join();
  }

  state.SetItemsProcessed(state.iterations() * items_per_iteration);
  state.SetBytesProcessed(state.iterations() * items_per_iteration *
                          sizeof(T));
}

// Multiple readers (fan-out) benchmark
template <typename T, std::size_t CAPACITY>
void BM_SingleProducerMultiConsumer(benchmark::State& state)
{
  const int num_readers = state.range(0);
  const int64_t items_per_iteration = state.range(1);

  for (auto _ : state)
  {
    dq::disruptor_queue<T, CAPACITY> queue;
    auto& writer = queue.create_writer();

    std::vector<typename dq::disruptor_queue<T, CAPACITY>::reader*> readers;
    for (int i = 0; i < num_readers; ++i)
    {
      readers.push_back(&queue.create_reader());
    }

    std::vector<std::thread> consumers;
    consumers.reserve(num_readers);

    for (int i = 0; i < num_readers; ++i)
    {
      consumers.emplace_back([&, i]() {
        for (int64_t j = 0; j < items_per_iteration; ++j)
        {
          benchmark::DoNotOptimize(readers[i]->read());
        }
      });
    }

    for (int64_t i = 0; i < items_per_iteration; ++i)
    {
      writer.write(T{});
    }

    for (auto& t : consumers)
    {
      t.join();
    }
  }

  state.SetItemsProcessed(state.iterations() * items_per_iteration *
                          num_readers);
}

// Multiple writers benchmark - uses wall-clock timing for accuracy
template <typename T, std::size_t CAPACITY>
void BM_MultiProducerSingleConsumer(benchmark::State& state)
{
  const int num_writers = state.range(0);
  const int64_t items_per_writer = state.range(1);
  const int64_t total_items = num_writers * items_per_writer;

  double total_time_ns = 0;

  for (auto _ : state)
  {
    dq::disruptor_queue<T, CAPACITY> queue;

    std::vector<typename dq::disruptor_queue<T, CAPACITY>::writer*> writers;
    for (int i = 0; i < num_writers; ++i)
    {
      writers.push_back(&queue.create_writer());
    }

    auto& reader = queue.create_reader();

    std::barrier start_barrier(num_writers + 2);

    std::thread consumer([&]() {
      start_barrier.arrive_and_wait();
      for (int64_t i = 0; i < total_items; ++i)
      {
        benchmark::DoNotOptimize(reader.read());
      }
    });

    std::vector<std::thread> producers;
    producers.reserve(num_writers);

    for (int i = 0; i < num_writers; ++i)
    {
      producers.emplace_back([&, i]() {
        start_barrier.arrive_and_wait();
        for (int64_t j = 0; j < items_per_writer; ++j)
        {
          writers[i]->write(T{});
        }
      });
    }

    auto start = std::chrono::high_resolution_clock::now();
    start_barrier.arrive_and_wait();

    for (auto& t : producers)
    {
      t.join();
    }
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    total_time_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
  }

  const double avg_time_s = total_time_ns / state.iterations() / 1e9;
  state.counters["items_per_sec"] = total_items / avg_time_s;
  state.counters["ns_per_item"] = total_time_ns / state.iterations() / total_items;
}

// Latency benchmark - measures per-item latency in steady state
// Producer and consumer run continuously; measures time per write
template <typename T, std::size_t CAPACITY>
void BM_Latency(benchmark::State& state)
{
  dq::disruptor_queue<T, CAPACITY> queue;
  auto& writer = queue.create_writer();
  auto& reader = queue.create_reader();

  std::atomic<int64_t> items_to_consume{0};
  std::atomic<bool> stop{false};

  std::thread consumer([&]() {
    while (true)
    {
      // Wait for items
      while (items_to_consume.load(std::memory_order_acquire) == 0)
      {
        if (stop.load(std::memory_order_acquire))
        {
          return;
        }
      }

      benchmark::DoNotOptimize(reader.read());
      items_to_consume.fetch_sub(1, std::memory_order_release);
    }
  });

  for (auto _ : state)
  {
    items_to_consume.fetch_add(1, std::memory_order_release);
    writer.write(T{});

    // Wait for consumer to process (measures round-trip)
    while (items_to_consume.load(std::memory_order_acquire) > 0)
    {
    }
  }

  stop.store(true, std::memory_order_release);
  consumer.join();

  state.SetItemsProcessed(state.iterations());
}

// Ping-pong latency benchmark - measures true round-trip latency
// Two queues: request and response
template <typename T, std::size_t CAPACITY>
void BM_PingPongLatency(benchmark::State& state)
{
  dq::disruptor_queue<T, CAPACITY> request_queue;
  dq::disruptor_queue<T, CAPACITY> response_queue;

  auto& request_writer = request_queue.create_writer();
  auto& request_reader = request_queue.create_reader();
  auto& response_writer = response_queue.create_writer();
  auto& response_reader = response_queue.create_reader();

  std::atomic<bool> stop{false};

  // Server thread: reads request, writes response
  std::thread server([&]() {
    while (!stop.load(std::memory_order_acquire))
    {
      T msg = request_reader.read();
      response_writer.write(msg);
    }
  });

  // Warm up
  for (int i = 0; i < 1000; ++i)
  {
    request_writer.write(T{});
    benchmark::DoNotOptimize(response_reader.read());
  }

  for (auto _ : state)
  {
    request_writer.write(T{});
    benchmark::DoNotOptimize(response_reader.read());
  }

  // Stop server
  stop.store(true, std::memory_order_release);
  request_writer.write(T{});  // Unblock server
  server.join();

  state.SetItemsProcessed(state.iterations());
}

// Burst write then read benchmark - excludes setup from timing
template <typename T, std::size_t CAPACITY>
void BM_BurstWriteRead(benchmark::State& state)
{
  const int64_t burst_size = state.range(0);

  for (auto _ : state)
  {
    state.PauseTiming();

    dq::disruptor_queue<T, CAPACITY> queue;
    auto& writer = queue.create_writer();
    auto& reader = queue.create_reader();

    std::barrier start_barrier(2);

    std::thread consumer([&]() {
      start_barrier.arrive_and_wait();
      for (int64_t i = 0; i < burst_size; ++i)
      {
        benchmark::DoNotOptimize(reader.read());
      }
    });

    state.ResumeTiming();

    start_barrier.arrive_and_wait();

    for (int64_t i = 0; i < burst_size; ++i)
    {
      writer.write(T{});
    }

    consumer.join();
  }

  state.SetItemsProcessed(state.iterations() * burst_size);
}

// Contention benchmark - measures impact of writer contention
template <typename T, std::size_t CAPACITY>
void BM_WriterContention(benchmark::State& state)
{
  const int num_writers = state.range(0);
  constexpr int64_t kItemsPerWriter = 100000;
  const int64_t total_items = num_writers * kItemsPerWriter;

  double total_time_ns = 0;
  int64_t total_iterations = 0;

  for (auto _ : state)
  {
    dq::disruptor_queue<T, CAPACITY> queue;

    std::vector<typename dq::disruptor_queue<T, CAPACITY>::writer*> writers;
    for (int i = 0; i < num_writers; ++i)
    {
      writers.push_back(&queue.create_writer());
    }

    auto& reader = queue.create_reader();

    std::barrier start_barrier(num_writers + 1);
    std::atomic<int64_t> consumed{0};

    std::thread consumer([&]() {
      for (int64_t i = 0; i < total_items; ++i)
      {
        benchmark::DoNotOptimize(reader.read());
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });

    std::vector<std::thread> producers;
    producers.reserve(num_writers);

    for (int i = 0; i < num_writers; ++i)
    {
      producers.emplace_back([&, i]() {
        start_barrier.arrive_and_wait();
        for (int64_t j = 0; j < kItemsPerWriter; ++j)
        {
          writers[i]->write(T{});
        }
      });
    }

    auto start = std::chrono::high_resolution_clock::now();
    start_barrier.arrive_and_wait();

    for (auto& t : producers)
    {
      t.join();
    }
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    total_time_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    total_iterations++;
  }

  state.SetItemsProcessed(state.iterations() * total_items);
  state.counters["ns_per_item"] =
      total_time_ns / (total_iterations * total_items);
}

// ==================== BENCHMARK REGISTRATIONS ====================

// SPSC Throughput - Small payload
BENCHMARK(BM_SPSC_Throughput<SmallPayload, 1024>)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

// SPSC Throughput - Medium payload
BENCHMARK(BM_SPSC_Throughput<MediumPayload, 1024>)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// SPSC Throughput - Large payload
BENCHMARK(BM_SPSC_Throughput<LargePayload, 1024>)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// SPSC with different queue sizes
BENCHMARK(BM_SPSC_Throughput<SmallPayload, 256>)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SPSC_Throughput<SmallPayload, 4096>)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SPSC_Throughput<SmallPayload, 65536>)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// Fan-out: 1 producer, N consumers
BENCHMARK(BM_SingleProducerMultiConsumer<SmallPayload, 1024>)
    ->Args({2, 100000})
    ->Args({4, 100000})
    ->Args({8, 100000})
    ->Unit(benchmark::kMicrosecond);

// Fan-in: N producers, 1 consumer (fixed)
BENCHMARK(BM_MultiProducerSingleConsumer<SmallPayload, 1024>)
    ->Args({2, 50000})
    ->Args({4, 25000})
    ->Args({8, 12500})
    ->Unit(benchmark::kMicrosecond);

// Writer contention benchmark (constant total work)
BENCHMARK(BM_WriterContention<SmallPayload, 4096>)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);

// Latency benchmarks
BENCHMARK(BM_Latency<SmallPayload, 1024>)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_PingPongLatency<SmallPayload, 1024>)->Unit(benchmark::kNanosecond);

// Burst patterns (fixed - excludes thread setup)
BENCHMARK(BM_BurstWriteRead<SmallPayload, 1024>)
    ->Arg(64)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond);

}  // namespace
