// Debug labels and object names (low-end-preset ticket 04): emitted only with
// the debug-utils functions, never nested.

#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include "native/debug_labels.h"

using wwe::native::DebugLabels;

namespace {

std::vector<std::string> g_calls;

VKAPI_ATTR void VKAPI_CALL FakeBegin(VkCommandBuffer, const VkDebugUtilsLabelEXT* label) {
  g_calls.push_back(std::string("begin ") + label->pLabelName);
}
VKAPI_ATTR void VKAPI_CALL FakeEnd(VkCommandBuffer) { g_calls.push_back("end"); }
VKAPI_ATTR VkResult VKAPI_CALL FakeName(VkDevice, const VkDebugUtilsObjectNameInfoEXT* info) {
  g_calls.push_back(std::string("name ") + info->pObjectName);
  return VK_SUCCESS;
}

// Any non-null handle stands in for a command buffer; the fakes never use it.
VkCommandBuffer Cb(uintptr_t n) { return reinterpret_cast<VkCommandBuffer>(n); }

}  // namespace

TEST_CASE("without the debug-utils functions no label or name call is made") {
  g_calls.clear();
  DebugLabels none;
  CHECK_FALSE(none.enabled());
  none.Begin(Cb(1), "pass 1280x720 RGBA8");
  none.End(Cb(1));
  none.Name(VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE, 0x1234, "target");
  // A partial set (one function missing) counts as absent.
  DebugLabels partial({FakeBegin, FakeEnd, nullptr});
  CHECK_FALSE(partial.enabled());
  partial.Begin(Cb(1), "pass");
  CHECK(g_calls.empty());
}

TEST_CASE("a label wraps the work between Begin and End, and a new Begin closes the open one") {
  g_calls.clear();
  DebugLabels labels({FakeBegin, FakeEnd, FakeName});
  REQUIRE(labels.enabled());
  labels.Begin(Cb(1), "pass 2560x1440 RGBA8 sRGB +depth");
  labels.Begin(Cb(1), "resolve 2560x1440 RGBA8");
  labels.End(Cb(1));
  labels.End(Cb(1));  // nothing open: no call
  CHECK(g_calls == std::vector<std::string>{"begin pass 2560x1440 RGBA8 sRGB +depth", "end",
                                            "begin resolve 2560x1440 RGBA8", "end"});
}

TEST_CASE("objects are named, and a null handle is skipped") {
  g_calls.clear();
  DebugLabels labels({FakeBegin, FakeEnd, FakeName});
  labels.Name(VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE, 0x1234, "target 1280x720 RGBA8");
  labels.Name(VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE, 0, "nothing");
  CHECK(g_calls == std::vector<std::string>{"name target 1280x720 RGBA8"});
}
