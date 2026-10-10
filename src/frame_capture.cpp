#include "frame_capture.h"

#include <atomic>
#include <filesystem>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>

namespace wwe::frame_capture {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_requested{false};
uint32_t g_sequence = 0;

// Next to the session log when there is one, else in captures/ beside the
// executable.
std::filesystem::path CaptureDirectory() {
  const std::filesystem::path log = rex::cvar::GetFlagByName("log_file");
  return log.empty() ? std::filesystem::path("captures") : log.parent_path();
}

}  // namespace

void Configure() {
  rex::ui::RegisterBind("bind_capture_frame", "F12",
                        "Save the frame on screen as a PNG next to the log",
                        [] { g_requested.store(true, std::memory_order_release); });
}

bool IsCaptureRepeat(rex::ui::VirtualKey key, bool repeat) {
  return repeat &&
         key == rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName("bind_capture_frame"));
}

std::string TakeRequest(uint64_t frame) {
  if (!g_requested.exchange(false, std::memory_order_acq_rel)) {
    return {};
  }
  const std::filesystem::path dir = CaptureDirectory();
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  std::lock_guard lock(g_mutex);
  const std::string path = (dir / CaptureFileName(++g_sequence, frame)).string();
  REXLOG_INFO("capture: frame {} to {}", frame, path);
  return path;
}

}  // namespace wwe::frame_capture
