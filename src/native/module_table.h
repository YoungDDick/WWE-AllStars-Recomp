// A table of objects built once each, on whichever thread asks first: the
// shader pack's shader modules. Each entry has its own lock, so a slow build
// never holds up another entry. Builds made for a counted caller (the render
// thread) are counted, so the log shows any that the workers did not do first.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace wwe::native {

template <typename Handle>
class ModuleTable {
 public:
  explicit ModuleTable(size_t count = 0) : entries_(std::make_unique<Entry[]>(count)), size_(count) {}

  size_t size() const { return size_; }

  // Entries built so far (failed builds included), for progress.
  size_t Built() const { return built_.load(); }

  // Entry i, built with build(i) by the first caller; Handle{} if that build
  // failed (never retried). counted: this caller's build is counted.
  template <typename Build>
  Handle Get(size_t i, Build&& build, bool counted = false) {
    Entry& entry = entries_[i];
    if (entry.done.load(std::memory_order_acquire)) {
      return entry.handle;
    }
    std::lock_guard lock(entry.mutex);
    if (!entry.done.load(std::memory_order_relaxed)) {
      const auto start = std::chrono::steady_clock::now();
      entry.handle = build(i);
      entry.done.store(true, std::memory_order_release);
      ++built_;
      if (counted) {
        const uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count());
        std::lock_guard counted_lock(counted_mutex_);
        counted_ns_ += ns;
        ++counted_;
        ++counted_total_;
      }
    }
    return entry.handle;
  }

  // Workers: builds the next entry no worker has taken yet (Get if a caller
  // got there first); false once every entry is taken.
  template <typename Build>
  bool BuildNext(Build&& build) {
    const size_t i = next_.fetch_add(1, std::memory_order_relaxed);
    if (i >= size_) {
      return false;
    }
    Get(i, build);
    return true;
  }

  // Counted builds since the last call, and the time they took (taken
  // together, so a build finishing meanwhile lands in one call or the next).
  void TakeCounted(uint64_t& count, uint64_t& ns) {
    std::lock_guard lock(counted_mutex_);
    count = counted_;
    ns = counted_ns_;
    counted_ = counted_ns_ = 0;
  }

  // Counted builds over the table's life.
  uint64_t CountedTotal() const { return counted_total_.load(); }

 private:
  struct Entry {
    std::mutex mutex;
    std::atomic<bool> done{false};
    Handle handle{};
  };
  std::unique_ptr<Entry[]> entries_;
  size_t size_;
  std::atomic<size_t> next_{0};
  std::atomic<size_t> built_{0};
  std::mutex counted_mutex_;  // counted builds only: never on a worker's path
  uint64_t counted_ = 0;
  uint64_t counted_ns_ = 0;
  std::atomic<uint64_t> counted_total_{0};
};

}  // namespace wwe::native
