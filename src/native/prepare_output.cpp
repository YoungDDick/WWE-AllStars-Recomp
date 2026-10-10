#include "native/prepare_output.h"

#include <fmt/format.h>

namespace wwe::native::prepare {

namespace {

const char* StageName(Stage stage) {
  switch (stage) {
    case Stage::kModules: return "modules";
    case Stage::kLibraries: return "libraries";
    case Stage::kPipelines: return "pipelines";
  }
  return "unknown";
}

// The snapshot's stages, in order; the libraries only on the library path.
std::vector<Progress> StagesOf(const Snapshot& s) {
  std::vector<Progress> stages = {{Stage::kModules, s.modules_built, s.modules_total}};
  if (s.libraries_total) {
    stages.push_back({Stage::kLibraries, s.libraries_built, s.libraries_total});
  }
  stages.push_back({Stage::kPipelines, s.pipelines_done, s.pipelines_total});
  return stages;
}

bool Done(const Progress& progress) { return progress.done >= progress.total; }

}  // namespace

bool Finished(const Snapshot& snapshot) {
  for (const Progress& stage : StagesOf(snapshot)) {
    if (!Done(stage)) {
      return false;
    }
  }
  return true;
}

std::vector<Progress> StageTracker::Report(const Snapshot& snapshot) {
  const std::vector<Progress> stages = StagesOf(snapshot);
  size_t current = 0;
  while (current + 1 < stages.size() && Done(stages[current])) {
    ++current;
  }
  std::vector<Progress> lines;
  for (; stages_ended_ < current; ++stages_ended_) {
    if (reporter_.ShouldReport(stages[stages_ended_])) {
      lines.push_back(stages[stages_ended_]);
    }
  }
  if (reporter_.ShouldReport(stages[current])) {
    lines.push_back(stages[current]);
  }
  return lines;
}

bool ProgressReporter::ShouldReport(const Progress& progress) {
  const size_t percent = progress.total ? progress.done * 100 / progress.total : 100;
  const bool same_stage = any_ && stage_ == progress.stage;
  if (same_stage && (progress.done == done_ ||
                     (percent == percent_ && progress.done != progress.total))) {
    return false;
  }
  any_ = true;
  stage_ = progress.stage;
  done_ = progress.done;
  percent_ = percent;
  return true;
}

std::string HeaderLine() { return "wwe-prepare-pipelines 1\n"; }

std::string ProgressLine(const Progress& progress) {
  return fmt::format("progress stage={} done={} total={}\n", StageName(progress.stage),
                     progress.done, progress.total);
}

std::string ResultLine(const Result& result) {
  if (!result.reason.empty()) {
    return fmt::format("result status=failed reason={} ms={}\n", result.reason, result.ms);
  }
  std::string line = fmt::format("result status={} modules={} pipelines={} failed={} ms={}",
                                 result.failed ? "partial" : "ok", result.modules,
                                 result.pipelines, result.failed, result.ms);
  if (result.library_path) {
    line += fmt::format(" libraries={} libraries_failed={}", result.libraries,
                        result.libraries_failed);
  }
  return line + "\n";
}

int ExitCode(const Result& result) {
  if (!result.reason.empty()) {
    return 2;
  }
  return result.failed ? 3 : 0;
}

}  // namespace wwe::native::prepare
