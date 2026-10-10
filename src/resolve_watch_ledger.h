// Bookkeeping behind --wwe_resolve_read_watch (src/resolve_watch.cpp): which
// resolve destinations the guest CPU touches before the GPU resolves them
// again. Each resolve arms its destination's 4 KB pages; the first CPU access
// to an armed page disarms it and is counted for the target that armed it
// last. A read first means the CPU consumes the resolve result; a write first
// means the memory is being reused.

#pragma once

#include <cstdint>
#include <unordered_map>

namespace wwe::resolve_watch {

class Ledger {
 public:
  struct Target {
    uint64_t resolves = 0;
    uint64_t reads = 0;         // first access to a page after a resolve was a read
    uint64_t writes_first = 0;  // ... was a write
    uint64_t first_read_frame = 0;
    uint32_t last_length = 0;
    std::unordered_map<uint64_t, uint64_t> read_sites;   // host code address -> faults
    std::unordered_map<uint64_t, uint64_t> write_sites;
  };

  // A resolve wrote the destination texture at physical address target.
  // Arm each of its pages the caller can watch.
  void CountResolve(uint32_t target, uint32_t length) {
    Target& t = targets_[target];
    ++t.resolves;
    t.last_length = length;
  }
  void Arm(uint32_t page, uint32_t target) { armed_[page] = target; }

  // A guest CPU access faulted on physical page `page`. True if the page was
  // armed (the caller then lifts its protection); false if the fault is not ours.
  bool Fault(uint32_t page, bool is_write, uint64_t frame, uint64_t site) {
    const auto it = armed_.find(page);
    if (it == armed_.end()) {
      return false;
    }
    Target& t = targets_[it->second];
    armed_.erase(it);
    if (is_write) {
      ++t.writes_first;
      ++t.write_sites[site];
    } else {
      if (!t.reads) {
        t.first_read_frame = frame;
      }
      ++t.reads;
      ++t.read_sites[site];
    }
    return true;
  }

  bool Armed(uint32_t page) const { return armed_.contains(page); }

  const std::unordered_map<uint32_t, Target>& Targets() const { return targets_; }

 private:
  std::unordered_map<uint32_t, uint32_t> armed_;  // physical page -> target
  std::unordered_map<uint32_t, Target> targets_;
};

}  // namespace wwe::resolve_watch
