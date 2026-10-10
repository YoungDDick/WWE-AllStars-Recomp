#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include "native/prepare_output.h"

namespace prepare = wwe::native::prepare;

TEST_CASE("prepare output starts with a versioned header line") {
  CHECK(prepare::HeaderLine() == "wwe-prepare-pipelines 1\n");
}

TEST_CASE("a progress line names the stage and its done and total counts") {
  CHECK(prepare::ProgressLine({prepare::Stage::kModules, 120, 3400}) ==
        "progress stage=modules done=120 total=3400\n");
  CHECK(prepare::ProgressLine({prepare::Stage::kPipelines, 40, 165}) ==
        "progress stage=pipelines done=40 total=165\n");
}

TEST_CASE("progress is reported at the start, at each whole percent and at the end") {
  prepare::ProgressReporter reporter;
  std::vector<size_t> reported;
  for (size_t done = 0; done <= 1000; ++done) {
    if (reporter.ShouldReport({prepare::Stage::kPipelines, done, 1000})) {
      reported.push_back(done);
    }
  }
  REQUIRE(reported.size() == 101);
  CHECK(reported.front() == 0);
  CHECK(reported[1] == 10);
  CHECK(reported.back() == 1000);
}

TEST_CASE("unchanged progress is reported once") {
  prepare::ProgressReporter reporter;
  CHECK(reporter.ShouldReport({prepare::Stage::kModules, 3, 7}));
  CHECK_FALSE(reporter.ShouldReport({prepare::Stage::kModules, 3, 7}));
}

TEST_CASE("a new stage is reported from its start, an empty one as done") {
  prepare::ProgressReporter reporter;
  CHECK(reporter.ShouldReport({prepare::Stage::kModules, 7, 7}));
  CHECK(reporter.ShouldReport({prepare::Stage::kPipelines, 0, 0}));
  CHECK_FALSE(reporter.ShouldReport({prepare::Stage::kPipelines, 0, 0}));
}

TEST_CASE("a few items still report every one") {
  prepare::ProgressReporter reporter;
  size_t reported = 0;
  for (size_t done = 0; done <= 3; ++done) {
    reported += reporter.ShouldReport({prepare::Stage::kPipelines, done, 3});
  }
  CHECK(reported == 4);
}

TEST_CASE("every pipeline built is an ok result with exit code 0") {
  prepare::Result result;
  result.modules = 3400;
  result.pipelines = 165;
  result.failed = 0;
  result.ms = 12345;
  CHECK(prepare::ResultLine(result) ==
        "result status=ok modules=3400 pipelines=165 failed=0 ms=12345\n");
  CHECK(prepare::ExitCode(result) == 0);
}

TEST_CASE("pipelines that failed to build make a partial result with exit code 3") {
  prepare::Result result;
  result.modules = 3400;
  result.pipelines = 163;
  result.failed = 2;
  result.ms = 9000;
  CHECK(prepare::ResultLine(result) ==
        "result status=partial modules=3400 pipelines=163 failed=2 ms=9000\n");
  CHECK(prepare::ExitCode(result) == 3);
}

TEST_CASE("a prepare that could not run names its reason and exits with code 2") {
  prepare::Result result;
  result.reason = "missing_shader_pack";
  result.ms = 40;
  CHECK(prepare::ResultLine(result) == "result status=failed reason=missing_shader_pack ms=40\n");
  CHECK(prepare::ExitCode(result) == 2);
}

namespace {

std::vector<std::string> Lines(prepare::StageTracker& tracker, const prepare::Snapshot& snapshot) {
  std::vector<std::string> lines;
  for (const prepare::Progress& progress : tracker.Report(snapshot)) {
    lines.push_back(prepare::ProgressLine(progress));
  }
  return lines;
}

}  // namespace

TEST_CASE("on the library path the shader libraries are a stage between modules and pipelines") {
  prepare::StageTracker tracker;
  CHECK(Lines(tracker, {.modules_built = 10, .modules_total = 20, .libraries_total = 400,
                        .pipelines_total = 5}) ==
        std::vector<std::string>{"progress stage=modules done=10 total=20\n"});
  CHECK(Lines(tracker, {.modules_built = 20, .modules_total = 20, .libraries_built = 100,
                        .libraries_total = 400, .pipelines_total = 5}) ==
        std::vector<std::string>{"progress stage=modules done=20 total=20\n",
                                 "progress stage=libraries done=100 total=400\n"});
  CHECK(Lines(tracker, {.modules_built = 20, .modules_total = 20, .libraries_built = 400,
                        .libraries_total = 400, .pipelines_done = 2, .pipelines_total = 5}) ==
        std::vector<std::string>{"progress stage=libraries done=400 total=400\n",
                                 "progress stage=pipelines done=2 total=5\n"});
}

TEST_CASE("without pipeline libraries there is no libraries stage") {
  prepare::StageTracker tracker;
  CHECK(Lines(tracker, {.modules_built = 20, .modules_total = 20, .pipelines_done = 3,
                        .pipelines_total = 165}) ==
        std::vector<std::string>{"progress stage=modules done=20 total=20\n",
                                 "progress stage=pipelines done=3 total=165\n"});
}

TEST_CASE("the prepare is finished once modules, libraries and pipelines are all done") {
  CHECK_FALSE(prepare::Finished({.modules_built = 20, .modules_total = 20, .libraries_built = 399,
                                 .libraries_total = 400, .pipelines_done = 5,
                                 .pipelines_total = 5}));
  CHECK_FALSE(prepare::Finished({.modules_built = 19, .modules_total = 20}));
  CHECK(prepare::Finished({.modules_built = 20, .modules_total = 20, .libraries_built = 400,
                           .libraries_total = 400, .pipelines_done = 5, .pipelines_total = 5}));
  CHECK(prepare::Finished({.modules_built = 20, .modules_total = 20}));
}

TEST_CASE("an empty pipelines stage is still reported once as done") {
  prepare::StageTracker tracker;
  CHECK(Lines(tracker, {.modules_built = 2, .modules_total = 2}) ==
        std::vector<std::string>{"progress stage=modules done=2 total=2\n",
                                 "progress stage=pipelines done=0 total=0\n"});
  CHECK(Lines(tracker, {.modules_built = 2, .modules_total = 2}).empty());
}

TEST_CASE("on the library path the result adds the libraries built and failed after the "
          "fields it always has") {
  prepare::Result result;
  result.modules = 2024;
  result.pipelines = 125;
  result.ms = 7000;
  result.library_path = true;
  result.libraries = 4440;
  result.libraries_failed = 4;
  CHECK(prepare::ResultLine(result) ==
        "result status=ok modules=2024 pipelines=125 failed=0 ms=7000 libraries=4440 "
        "libraries_failed=4\n");
  // Only planned pipelines decide the status: a library that failed costs a key nobody
  // met a full compile, never a known key's pipeline.
  CHECK(prepare::ExitCode(result) == 0);
}
