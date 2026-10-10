#include "resolve_watch.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(wwe_resolve_read_watch, false, "WWEAllStars",
                    "Research: log guest CPU reads of resolve destinations (slow; Emulated only; "
                    "Windows only)");

#ifndef _WIN32

// The watch relies on Win32 page protection and vectored exception handling;
// other platforms get no-ops (ADR-0006: Win32 APIs stay out of portable code).
namespace wwe::resolve_watch {
void Configure(rex::Runtime*) {}
void OnResolve(const uint8_t*, uint32_t, uint64_t) {}
void LogPeriodic(uint64_t) {}
}  // namespace wwe::resolve_watch

#else

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <windows.h>
#include <dbghelp.h>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "native/textures.h"
#include "resolve_watch_ledger.h"

#pragma comment(lib, "dbghelp.lib")

namespace wwe::resolve_watch {

namespace {

constexpr uint32_t kViews[] = {0xA0000000u, 0xC0000000u, 0xE0000000u};
constexpr uint32_t kPhysicalSize = 0x20000000u;
constexpr uint32_t kPageShift = 12;
constexpr uint32_t kTextureFetchConstant = 0x1C;

struct State {
  std::mutex mutex;
  Ledger ledger;
  std::array<uint8_t*, 3> views{};
  // Physical page -> its protection in each view before we armed it.
  std::unordered_map<uint32_t, std::array<DWORD, 3>> saved;
  // Physical address -> width, height, format of the last resolve into it.
  std::unordered_map<uint32_t, std::array<uint32_t, 3>> shapes;
  std::atomic<uint64_t> frame{0};
  bool symbols = false;
  bool self_tested = false;
} g;

uint32_t LoadBE32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// The physical page a host address falls in, if it is in a guest CPU view.
bool PageOf(uintptr_t host, uint32_t& page) {
  for (uint8_t* view : g.views) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(view);
    if (host >= start && host - start < kPhysicalSize) {
      page = uint32_t((host - start) >> kPageShift);
      return true;
    }
  }
  return false;
}

// Puts back the protection a page had before it was armed, in each view that
// still has ours: if the SDK reprotected a view meanwhile (its write watch),
// that view keeps the SDK's protection.
void Restore(uint32_t page) {
  const auto it = g.saved.find(page);
  if (it == g.saved.end()) {
    return;
  }
  for (size_t v = 0; v < g.views.size(); ++v) {
    uint8_t* host = g.views[v] + (size_t(page) << kPageShift);
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(host, &mbi, sizeof(mbi)) && mbi.Protect == PAGE_NOACCESS) {
      DWORD old;
      VirtualProtect(host, 1u << kPageShift, it->second[v], &old);
    }
  }
  g.saved.erase(it);
}

// Whether the faulting access would now succeed: another thread disarmed the
// page between this thread's fault and its handler.
bool AccessNowAllowed(const void* host, bool is_write) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(host, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
      (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  return !is_write || (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  uint32_t page;
  if (!PageOf(uintptr_t(record->ExceptionInformation[1]), page)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  std::lock_guard lock(g.mutex);
  const bool is_write = record->ExceptionInformation[0] == 1;
  if (!g.ledger.Fault(page, is_write, g.frame.load(std::memory_order_relaxed),
                      info->ContextRecord->Rip)) {
    // Not armed: either another thread just disarmed it (retry) or the fault
    // is someone else's (the SDK's write watch, or a real violation).
    return AccessNowAllowed(reinterpret_cast<const void*>(record->ExceptionInformation[1]),
                            is_write)
               ? EXCEPTION_CONTINUE_EXECUTION
               : EXCEPTION_CONTINUE_SEARCH;
  }
  Restore(page);
  return EXCEPTION_CONTINUE_EXECUTION;
}

std::string Symbol(uint64_t address) {
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (g.symbols && SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
    return fmt::format("{}+{:X}", symbol->Name, displacement);
  }
  return fmt::format("{:X}", address);
}

std::string Sites(const std::unordered_map<uint64_t, uint64_t>& sites) {
  std::vector<std::pair<uint64_t, uint64_t>> top;
  for (const auto& [site, n] : sites) top.emplace_back(n, site);
  std::sort(top.rbegin(), top.rend());
  std::string out;
  for (size_t i = 0; i < top.size() && i < 6; ++i) {
    out += fmt::format(" {}={}", Symbol(top[i].second), top[i].first);
  }
  return out;
}

// Makes a page fault in every guest CPU view and arms it for target.
// Caller holds g.mutex.
void ArmPage(uint32_t page, uint32_t target) {
  if (g.saved.contains(page)) {
    g.ledger.Arm(page, target);  // still inaccessible from an earlier resolve
    return;
  }
  std::array<DWORD, 3> protect{};
  for (size_t v = 0; v < g.views.size(); ++v) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(g.views[v] + (size_t(page) << kPageShift), &mbi, sizeof(mbi)) ||
        mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      return;  // the guest's own protection; its real faults must stay real
    }
    protect[v] = mbi.Protect;
  }
  for (size_t v = 0; v < g.views.size(); ++v) {
    DWORD old;
    VirtualProtect(g.views[v] + (size_t(page) << kPageShift), 1u << kPageShift, PAGE_NOACCESS,
                   &old);
  }
  g.saved[page] = protect;
  g.ledger.Arm(page, target);
}

// Positive control, once per run: a read through a guest view of an armed
// page must reach the ledger (as pseudo-target 0, left out of the report).
void SelfTest(uint32_t page) {
  g.self_tested = true;
  {
    std::lock_guard lock(g.mutex);
    ArmPage(page, 0);
  }
  const uint8_t value = *reinterpret_cast<volatile const uint8_t*>(
      g.views[2] + (size_t(page) << kPageShift));
  std::lock_guard lock(g.mutex);
  const auto it = g.ledger.Targets().find(0);
  const bool ok = it != g.ledger.Targets().end() && it->second.reads == 1;
  REXLOG_INFO("resolve-watch self-test page={:05X} read {:02X}: {}", page, value,
              ok ? "fault recorded" : "FAILED, no fault recorded");
}

}  // namespace

void Configure(rex::Runtime* runtime) {
  if (!REXCVAR_GET(wwe_resolve_read_watch)) {
    return;
  }
  for (size_t v = 0; v < g.views.size(); ++v) {
    g.views[v] = runtime->memory()->TranslateVirtual(kViews[v]);
  }
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  g.symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  // First in the chain: the SDK's own handler (write watches, MMIO) must not
  // see faults on pages this watch armed.
  AddVectoredExceptionHandler(1, OnException);
  REXLOG_INFO("resolve-watch: armed on each resolve; views {} {} {}",
              static_cast<void*>(g.views[0]), static_cast<void*>(g.views[1]),
              static_cast<void*>(g.views[2]));
}

void OnResolve(const uint8_t* base, uint32_t destination, uint64_t frame) {
  g.frame.store(frame, std::memory_order_relaxed);
  if (!REXCVAR_GET(wwe_resolve_read_watch) || !destination || !g.views[0]) {
    return;
  }
  const uint8_t* fetch_bytes = base + destination + kTextureFetchConstant;
  const native::textures::FetchConstant fetch = native::textures::LoadFetchConstant(fetch_bytes);
  const native::textures::GuestRange range = native::textures::Level0Range(fetch);
  if (!range.length || range.physical >= kPhysicalSize) {
    return;
  }
  const uint32_t first = range.physical >> kPageShift;
  const uint32_t last = std::min<uint32_t>(range.physical + range.length - 1, kPhysicalSize - 1) >>
                        kPageShift;
  const uint32_t size = LoadBE32(fetch_bytes + 8);
  const std::array<uint32_t, 3> shape = {(size & 0x1FFF) + 1, ((size >> 13) & 0x1FFF) + 1,
                                         LoadBE32(fetch_bytes + 4) & 0x3F};
  if (!g.self_tested) {
    SelfTest(first);
  }
  std::lock_guard lock(g.mutex);
  g.ledger.CountResolve(range.physical, range.length);
  g.shapes[range.physical] = shape;
  for (uint32_t page = first; page <= last; ++page) {
    ArmPage(page, range.physical);
  }
}

void LogPeriodic(uint64_t frame) {
  if (!REXCVAR_GET(wwe_resolve_read_watch) || frame % 600) {
    return;
  }
  std::lock_guard lock(g.mutex);
  size_t targets = 0, touched = 0;
  for (const auto& [target, t] : g.ledger.Targets()) {
    if (!target) continue;  // the self-test page
    ++targets;
    touched += t.reads || t.writes_first;
    const auto& shape = g.shapes[target];
    REXLOG_INFO("resolve-watch target={:08X} {}x{} fmt={} len={:X} resolves={} cpu_reads={} "
                "first_read_frame={} writes_first={} read_sites:{} write_sites:{}",
                target, shape[0], shape[1], shape[2], t.last_length, t.resolves, t.reads,
                t.first_read_frame, t.writes_first, Sites(t.read_sites), Sites(t.write_sites));
  }
  REXLOG_INFO("resolve-watch frame={} targets={} touched_by_cpu={} armed_pages={}", frame,
              targets, touched, g.saved.size());
}

}  // namespace wwe::resolve_watch

#endif  // _WIN32
