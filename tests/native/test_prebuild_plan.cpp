#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "native/prebuild_plan.h"

using wwe::native::PipelineKey;
using wwe::native::ShaderPackEntry;
using namespace wwe::native::prebuild;

namespace {

constexpr uint64_t kConverter = 0x19BC9795A8B5E348;
constexpr auto kNatural = wwe::native::ColourMode::kNatural;

PipelineKey Key(uint64_t vertex_shader, uint64_t pixel_shader, uint32_t spec_constants = 0,
                uint32_t topology = 3) {
  PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  key.vertex_shader = vertex_shader;
  key.pixel_shader = pixel_shader;
  key.spec_constants = spec_constants;
  key.topology = topology;
  return key;
}

bool Same(const PipelineKey& a, const PipelineKey& b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool Contains(const std::vector<PipelineKey>& keys, const PipelineKey& key) {
  for (const PipelineKey& k : keys) {
    if (Same(k, key)) {
      return true;
    }
  }
  return false;
}

// Shaders 0x10, 0x11 (vertex) and 0x20, 0x21 (pixel); 0x20 reads spec
// constant bits 0x3, 0x10 reads bit 0x4, the others none.
const std::vector<ShaderPackEntry> kPack = {
    {0x10, 0, 0, 0x4}, {0x11, 0, 0, 0}, {0x20, 0, 0, 0x3}, {0x21, 0, 0, 0}};

}  // namespace

TEST_CASE("without pipeline libraries every usable key is planned as a full pipeline") {
  const std::vector<PipelineKey> stored = {Key(0x10, 0x20), Key(0x11, 0x21), Key(0x11, 0)};
  const KeySource sources[] = {{kConverter, stored}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  CHECK(plan.libraries.empty());
  REQUIRE(plan.pipelines.size() == 3);
  for (const PipelineKey& key : stored) {
    CHECK(Contains(plan.pipelines, key));
  }
  CHECK(plan.missing_shader == 0);
  CHECK(plan.other_converter == 0);
}

TEST_CASE("keys for a missing shader or of another converter are dropped and counted") {
  const std::vector<PipelineKey> stored = {Key(0x10, 0x20), Key(0x10, 0x99), Key(0x98, 0)};
  const std::vector<PipelineKey> old_list = {Key(0x11, 0x21), Key(0x10, 0x21)};
  const KeySource sources[] = {{kConverter, stored}, {0x1234, old_list}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  REQUIRE(plan.pipelines.size() == 1);
  CHECK(Same(plan.pipelines[0], Key(0x10, 0x20)));
  CHECK(plan.missing_shader == 2);
  CHECK(plan.other_converter == 2);
}

TEST_CASE("shipped and stored keys merge without duplicates") {
  const std::vector<PipelineKey> shipped = {Key(0x10, 0x20), Key(0x11, 0x21), Key(0x10, 0x20)};
  const std::vector<PipelineKey> stored = {Key(0x11, 0x21), Key(0x11, 0x21, 0, 4)};
  const KeySource sources[] = {{kConverter, shipped}, {kConverter, stored}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  REQUIRE(plan.pipelines.size() == 3);
  CHECK(Contains(plan.pipelines, Key(0x10, 0x20)));
  CHECK(Contains(plan.pipelines, Key(0x11, 0x21)));
  CHECK(Contains(plan.pipelines, Key(0x11, 0x21, 0, 4)));
}

TEST_CASE("an empty store plus the shipped list plans the whole list") {
  const std::vector<PipelineKey> shipped = {Key(0x10, 0x20), Key(0x11, 0x21), Key(0x10, 0)};
  const KeySource sources[] = {{kConverter, shipped}, {kConverter, {}}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  REQUIRE(plan.pipelines.size() == 3);
  for (const PipelineKey& key : shipped) {
    CHECK(Contains(plan.pipelines, key));
  }
}

TEST_CASE("with pipeline libraries one library per shader and spec-constant value, and an "
          "optimized pipeline per key") {
  // 0x10 reads bit 0x4 and 0x20 bits 0x3: spec 0x5 and 0x4 give 0x10 one
  // library (0x4), 0x20 two (0x1, 0x0). 0x11 and 0x21 read none.
  const std::vector<PipelineKey> keys = {Key(0x10, 0x20, 0x5), Key(0x10, 0x20, 0x4),
                                         Key(0x11, 0x21, 0x7), Key(0x11, 0)};
  const KeySource sources[] = {{kConverter, keys}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{true, true}, kNatural);
  const std::vector<ShaderVariant> expected = {{0x10, 0x4}, {0x11, 0}, {0x20, 0x0}, {0x20, 0x1},
                                               {0x21, 0}};
  CHECK(plan.libraries == expected);
  CHECK(plan.pipelines.size() == 4);
}

TEST_CASE("on the library path keys that differ only in dynamic state share one optimized "
          "pipeline") {
  PipelineKey culled = Key(0x10, 0x20);
  culled.cull = 2;
  culled.strides[0] = 32;
  PipelineKey wider = Key(0x10, 0x20);
  wider.strides[0] = 48;
  PipelineKey narrow = Key(0x10, 0x20);
  narrow.strides[0] = 16;
  const std::vector<PipelineKey> keys = {culled, wider, narrow, Key(0x11, 0x21)};
  const KeySource sources[] = {{kConverter, keys}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{true, true}, kNatural);
  CHECK(plan.pipelines.size() == 2);
  // The full-pipeline path keeps every key: there the state is baked in.
  const Plan full = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  CHECK(full.pipelines.size() == 4);
}

TEST_CASE("on the library path every pack shader no key uses gets a library per spec-constant "
          "value it can take") {
  const std::vector<PipelineKey> keys = {Key(0x10, 0x20, 0x1)};
  const KeySource sources[] = {{kConverter, keys}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{true, true}, kNatural);
  const std::vector<ShaderVariant> keyed = {{0x10, 0x0}, {0x20, 0x1}};
  CHECK(plan.libraries == keyed);
  // 0x10 reads bit 0x4, 0x20 bits 0x3: the values no key used yet.
  const std::vector<ShaderVariant> rest = {{0x10, 0x4}, {0x11, 0},   {0x20, 0x0},
                                           {0x20, 0x2}, {0x20, 0x3}, {0x21, 0}};
  CHECK(plan.pack_libraries == rest);
  const Plan full = MakePlan(kPack, kConverter, sources, DeviceFeatures{}, kNatural);
  CHECK(full.pack_libraries.empty());
}

TEST_CASE("pipeline libraries without fast linking plan full pipelines only") {
  const std::vector<PipelineKey> keys = {Key(0x10, 0x20)};
  const KeySource sources[] = {{kConverter, keys}};
  const Plan plan = MakePlan(kPack, kConverter, sources, DeviceFeatures{true, false}, kNatural);
  CHECK(plan.libraries.empty());
  CHECK(plan.pipelines.size() == 1);
}

namespace {

// A key line as scripts/pipeline_keys.py writes it: the vertex shader's low
// byte, pixel shader 0x20, then zeros.
std::string KeyLine(char vertex_digit) {
  std::string line(2 * sizeof(PipelineKey), '0');
  line[1] = vertex_digit;
  line[16] = '2';
  return line + "\n";
}

const std::string kListHeader =
    "# comment\n"
    "wwe-pipeline-keys 1\n"
    "key-size 344\n"
    "converter 19bc9795a8b5e348\n";

}  // namespace

TEST_CASE("the shipped key list is read as plain keys with its converter") {
  const KeyListLoad load = ReadKeyList(kListHeader + KeyLine('1') + KeyLine('2'));
  REQUIRE(load.ok);
  CHECK(load.converter_id == kConverter);
  REQUIRE(load.keys.size() == 2);
  CHECK(Same(load.keys[0], Key(0x01, 0x20, 0, 0)));
  CHECK(Same(load.keys[1], Key(0x02, 0x20, 0, 0)));
}

TEST_CASE("a key list of another version, key size or with a bad line is refused") {
  CHECK_FALSE(ReadKeyList("").ok);
  CHECK_FALSE(ReadKeyList("wwe-pipeline-keys 2\nkey-size 344\nconverter 1\n").ok);
  CHECK_FALSE(ReadKeyList("wwe-pipeline-keys 1\nkey-size 300\nconverter 1\n").ok);
  CHECK_FALSE(ReadKeyList(kListHeader + "abc\n").ok);
  CHECK_FALSE(ReadKeyList(kListHeader + std::string(2 * sizeof(PipelineKey), 'x') + "\n").ok);
}

TEST_CASE("a key list whose converter id is empty or longer than 64 bits is refused") {
  const std::string body = "wwe-pipeline-keys 1\nkey-size 344\n";
  CHECK_FALSE(ReadKeyList(body + "converter \n" + KeyLine('1')).ok);
  CHECK_FALSE(ReadKeyList(body + "converter 119bc9795a8b5e348\n" + KeyLine('1')).ok);
  CHECK(ReadKeyList(body + "converter 19bc9795a8b5e348\n" + KeyLine('1')).ok);
}

TEST_CASE("prebuild progress counts only planned keys, each once") {
  const std::vector<PipelineKey> planned = {Key(0x10, 0x20), Key(0x11, 0x21)};
  Progress progress;
  progress.Start(planned);
  CHECK(progress.total() == 2);
  CHECK(progress.done() == 0);
  // A key the render thread queued meanwhile is not part of the plan.
  CHECK_FALSE(progress.Finish(Key(0x11, 0), false));
  CHECK(progress.done() == 0);
  CHECK_FALSE(progress.Finish(Key(0x10, 0x20), false));
  CHECK_FALSE(progress.Finish(Key(0x10, 0x20), false));
  CHECK(progress.done() == 1);
  // The last planned key reports that the plan is done.
  CHECK(progress.Finish(Key(0x11, 0x21), true));
  CHECK(progress.done() == 2);
  CHECK(progress.failed() == 1);
}

TEST_CASE("prebuild progress without a plan is never done") {
  Progress progress;
  CHECK_FALSE(progress.Finish(Key(0x10, 0x20), false));
  CHECK(progress.total() == 0);
  CHECK(progress.done() == 0);
}

namespace {

using wwe::native::ColourMode;

// Pixel shader 0x22 reads alpha test (0x2) and the PWL gamma output bit (0x8).
const std::vector<ShaderPackEntry> kColourPack = {
    {0x10, 0, 0, 0x4}, {0x11, 0, 0, 0}, {0x21, 0, 0, 0}, {0x22, 0, 0, 0xA}};

PipelineKey Into(PipelineKey key, VkFormat color_format) {
  key.color_format = color_format;
  return key;
}

}  // namespace

TEST_CASE("Original plans a key into a gamma target with the PWL output bit") {
  const std::vector<PipelineKey> shipped = {Into(Key(0x11, 0x22, 0x2), VK_FORMAT_R8G8B8A8_SRGB)};
  const KeySource sources[] = {{kConverter, shipped}};
  const Plan plan =
      MakePlan(kColourPack, kConverter, sources, DeviceFeatures{}, ColourMode::kOriginal);
  REQUIRE(plan.pipelines.size() == 1);
  CHECK(Same(plan.pipelines[0], Into(Key(0x11, 0x22, 0xA), VK_FORMAT_R8G8B8A8_SRGB)));
}

TEST_CASE("Natural plans a key an Original session stored as its Natural variant") {
  const std::vector<PipelineKey> stored = {Into(Key(0x11, 0x22, 0xA), VK_FORMAT_R8G8B8A8_SRGB),
                                           Into(Key(0x11, 0x22, 0x2), VK_FORMAT_R8G8B8A8_SRGB)};
  const KeySource sources[] = {{kConverter, stored}};
  const Plan plan =
      MakePlan(kColourPack, kConverter, sources, DeviceFeatures{}, ColourMode::kNatural);
  REQUIRE(plan.pipelines.size() == 1);
  CHECK(Same(plan.pipelines[0], Into(Key(0x11, 0x22, 0x2), VK_FORMAT_R8G8B8A8_SRGB)));
}

TEST_CASE("plain-target keys, or keys whose shader has no PWL output, are the same in both modes") {
  const std::vector<PipelineKey> keys = {Into(Key(0x11, 0x22), VK_FORMAT_R8G8B8A8_UNORM),
                                         Into(Key(0x11, 0x21), VK_FORMAT_R8G8B8A8_SRGB)};
  const KeySource sources[] = {{kConverter, keys}};
  for (const ColourMode mode : {ColourMode::kOriginal, ColourMode::kNatural}) {
    const Plan plan = MakePlan(kColourPack, kConverter, sources, DeviceFeatures{}, mode);
    REQUIRE(plan.pipelines.size() == 2);
    CHECK(Contains(plan.pipelines, keys[0]));
    CHECK(Contains(plan.pipelines, keys[1]));
  }
}

TEST_CASE("only Original builds the PWL output libraries of the pack's shaders") {
  const KeySource sources[] = {{kConverter, {}}};
  const DeviceFeatures gpl{true, true};
  const auto values_of_0x22 = [](const Plan& plan) {
    std::vector<uint32_t> values;
    for (const ShaderVariant& v : plan.pack_libraries) {
      if (v.shader == 0x22) values.push_back(v.spec_constants);
    }
    return values;
  };
  CHECK(values_of_0x22(MakePlan(kColourPack, kConverter, sources, gpl, ColourMode::kNatural)) ==
        std::vector<uint32_t>{0x0, 0x2});
  CHECK(values_of_0x22(MakePlan(kColourPack, kConverter, sources, gpl, ColourMode::kOriginal)) ==
        std::vector<uint32_t>{0x0, 0x2, 0x8, 0xA});
}
