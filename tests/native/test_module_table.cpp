#include <catch_amalgamated.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "native/module_table.h"

using wwe::native::ModuleTable;

TEST_CASE("an entry is built once however many threads ask for it") {
  ModuleTable<uint64_t> table(4);
  std::atomic<int> builds{0};
  const auto build = [&](size_t i) {
    ++builds;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return uint64_t(100 + i);
  };
  std::vector<uint64_t> got(8);
  {
    std::vector<std::jthread> threads;
    for (size_t t = 0; t < got.size(); ++t) {
      threads.emplace_back([&, t] { got[t] = table.Get(2, build); });
    }
  }
  CHECK(builds == 1);
  for (uint64_t handle : got) {
    CHECK(handle == 102);
  }
}

TEST_CASE("the table counts the entries built so far, failed ones too") {
  ModuleTable<uint64_t> table(3);
  CHECK(table.Built() == 0);
  table.Get(1, [](size_t) { return uint64_t(7); });
  table.Get(1, [](size_t) { return uint64_t(7); });
  CHECK(table.Built() == 1);
  table.Get(0, [](size_t) { return uint64_t(0); });  // a failed build
  CHECK(table.Built() == 2);
}

TEST_CASE("a slow build does not hold up another entry") {
  ModuleTable<uint64_t> table(4);
  std::atomic<bool> release{false};
  std::atomic<bool> slow_started{false};
  std::jthread slow([&] {
    table.Get(0, [&](size_t) {
      slow_started = true;
      while (!release) {
        std::this_thread::yield();
      }
      return uint64_t(1);
    });
  });
  while (!slow_started) {
    std::this_thread::yield();
  }
  CHECK(table.Get(1, [](size_t) { return uint64_t(7); }) == 7);
  release = true;
}

TEST_CASE("a failed build is not retried") {
  ModuleTable<uint64_t> table(2);
  int builds = 0;
  const auto failing = [&](size_t) {
    ++builds;
    return uint64_t(0);
  };
  CHECK(table.Get(1, failing) == 0);
  CHECK(table.Get(1, failing) == 0);
  CHECK(builds == 1);
}

TEST_CASE("the workers build every entry, each once") {
  ModuleTable<uint64_t> table(50);
  std::atomic<int> builds{0};
  const auto build = [&](size_t i) {
    ++builds;
    return uint64_t(i + 1);
  };
  {
    std::vector<std::jthread> workers;
    for (int w = 0; w < 4; ++w) {
      workers.emplace_back([&] {
        while (table.BuildNext(build)) {
        }
      });
    }
  }
  CHECK(builds == 50);
  CHECK(table.Get(49, build) == 50);
  CHECK(builds == 50);
}

TEST_CASE("only builds made for a counted caller are counted") {
  ModuleTable<uint64_t> table(3);
  const auto build = [](size_t i) { return uint64_t(i + 1); };
  table.BuildNext(build);  // entry 0, on a worker
  CHECK(table.Get(0, build, true) == 1);
  CHECK(table.Get(2, build, true) == 3);
  CHECK(table.Get(2, build, true) == 3);
  CHECK(table.Get(1, build) == 2);
  uint64_t count = 0, ns = 0;
  table.TakeCounted(count, ns);
  CHECK(count == 1);
  table.TakeCounted(count, ns);
  CHECK(count == 0);
  CHECK(table.CountedTotal() == 1);
}
