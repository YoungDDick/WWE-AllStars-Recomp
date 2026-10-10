// Debug labels for Native's GPU work and names for its main images (low-end-
// preset ticket 04), so RenderDoc, RGP and Nsight captures are readable.
// Emitted only when the instance has VK_EXT_debug_utils: without its
// functions every call here does nothing (tests/native/test_debug_labels.cpp).

#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan_core.h>

namespace wwe::native {

class DebugLabels {
 public:
  struct Functions {
    PFN_vkCmdBeginDebugUtilsLabelEXT begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT end = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT set_name = nullptr;
  };

  DebugLabels() = default;
  // All three or none: a partial set (a loader without the extension) stays off.
  explicit DebugLabels(const Functions& functions)
      : fn_(functions.begin && functions.end && functions.set_name ? functions : Functions{}) {}

  bool enabled() const { return fn_.begin != nullptr; }

  // Opens a label; labels do not nest here, so an open one is closed first.
  void Begin(VkCommandBuffer cb, const std::string& name) {
    if (!enabled()) return;
    End(cb);
    VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name.c_str();
    fn_.begin(cb, &label);
    open_ = cb;
  }

  void End(VkCommandBuffer cb) {
    if (!enabled() || open_ != cb || cb == VK_NULL_HANDLE) return;
    fn_.end(cb);
    open_ = VK_NULL_HANDLE;
  }

  void Name(VkDevice device, VkObjectType type, uint64_t handle, const std::string& name) const {
    if (!enabled() || !handle) return;
    VkDebugUtilsObjectNameInfoEXT info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    fn_.set_name(device, &info);
  }

 private:
  Functions fn_;
  VkCommandBuffer open_ = VK_NULL_HANDLE;
};

}  // namespace wwe::native
