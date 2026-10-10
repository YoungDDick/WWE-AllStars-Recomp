// The output of the game's prepare-pipelines mode (--wwe_prepare_pipelines),
// which the launcher reads: lines on standard output, then an exit code.
// Pure: it formats, and decides when to report, nothing else.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wwe::native::prepare {

// In order: the pack's shader modules, on the library path its shader
// libraries (one per shader and spec-constant value), then the planned
// pipelines.
enum class Stage { kModules, kLibraries, kPipelines };

struct Progress {
  Stage stage = Stage::kModules;
  size_t done = 0;
  size_t total = 0;
};

// The last line. A prepare that ran builds every module and planned
// pipeline; one that could not run (no shader pack, no Vulkan) has a reason,
// a renderer fallback name (renderer_fallback.h).
struct Result {
  std::string reason;  // empty: the prepare ran
  size_t modules = 0;
  size_t pipelines = 0;  // built
  size_t failed = 0;     // planned pipelines the driver could not build
  long long ms = 0;
  // On the library path the line adds the shader libraries built and failed.
  // A failed library leaves a key nobody met without a fast link (it compiles
  // in full instead); the status stays the planned pipelines'.
  bool library_path = false;
  size_t libraries = 0;
  size_t libraries_failed = 0;
};

// Keeps the output short: a stage's progress is reported at its start, at
// each whole percent and at its end, never twice the same.
class ProgressReporter {
 public:
  bool ShouldReport(const Progress& progress);

 private:
  bool any_ = false;
  Stage stage_ = Stage::kModules;
  size_t done_ = 0;
  size_t percent_ = 0;
};

// Every stage's counts at one moment. libraries_total is 0 off the library
// path, and that stage is then left out.
struct Snapshot {
  size_t modules_built = 0;
  size_t modules_total = 0;
  size_t libraries_built = 0;
  size_t libraries_total = 0;
  size_t pipelines_done = 0;
  size_t pipelines_total = 0;
};

bool Finished(const Snapshot& snapshot);

// The progress lines to print for a snapshot: the end of each stage the
// prepare left since the last call, then the stage it is in (the first one
// not done). Throttled like ProgressReporter.
class StageTracker {
 public:
  std::vector<Progress> Report(const Snapshot& snapshot);

 private:
  ProgressReporter reporter_;
  size_t stages_ended_ = 0;  // stages whose end has been reported
};

std::string HeaderLine();
std::string ProgressLine(const Progress& progress);
std::string ResultLine(const Result& result);
int ExitCode(const Result& result);

}  // namespace wwe::native::prepare
