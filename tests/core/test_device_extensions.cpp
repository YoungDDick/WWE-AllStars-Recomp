#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include <rex/ui/vulkan/device_extension_plan.h>

using rex::ui::vulkan::DescribeOptionalFeatures;
using rex::ui::vulkan::DeviceFeatureOffer;
using rex::ui::vulkan::DeviceOffer;
using rex::ui::vulkan::EnableOptionalFeatures;
using rex::ui::vulkan::OptionalFeatures;
using rex::ui::vulkan::DeviceRequest;
using rex::ui::vulkan::RequestOptionalExtensions;

namespace {

// The owner's AMD driver: pipeline library and present v2, no present v1.
DeviceOffer AmdOffer() {
  DeviceOffer offer;
  offer.vulkan_1_3 = true;
  offer.surface_capabilities2 = true;
  offer.pipeline_library = true;
  offer.graphics_pipeline_library = true;
  offer.present_id2 = true;
  offer.present_wait2 = true;
  return offer;
}

DeviceFeatureOffer AmdFeatures() {
  DeviceFeatureOffer f;
  f.graphicsPipelineLibrary = true;
  f.graphicsPipelineLibraryFastLinking = true;
  f.graphicsPipelineLibraryIndependentInterpolationDecoration = true;
  f.presentId2 = true;
  f.presentWait2 = true;
  return f;
}

}  // namespace

TEST_CASE("a device with the pipeline library gets it requested with its dependency") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  CHECK(r.pipeline_library);
  CHECK(r.graphics_pipeline_library);
}

TEST_CASE("the developer switch requests none of the optional extensions") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, true);
  CHECK_FALSE(r.pipeline_library);
  CHECK_FALSE(r.graphics_pipeline_library);
  CHECK_FALSE(r.present_id2);
  CHECK_FALSE(r.present_wait2);
}

TEST_CASE("present-wait v2 is requested as a pair when the instance can query surfaces") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  CHECK(r.present_id2);
  CHECK(r.present_wait2);
  CHECK_FALSE(r.present_id);
  CHECK_FALSE(r.present_wait);

  DeviceOffer no_caps2 = AmdOffer();
  no_caps2.surface_capabilities2 = false;
  const DeviceRequest without = RequestOptionalExtensions(no_caps2, true, true, false);
  CHECK_FALSE(without.present_id2);
  CHECK_FALSE(without.present_wait2);
}

TEST_CASE("present-wait v1 is requested when the device lists it, beside v2") {
  // NVIDIA lists both versions; the presenter chooses v2 first.
  DeviceOffer nvidia = AmdOffer();
  nvidia.present_id = true;
  nvidia.present_wait = true;
  const DeviceRequest r = RequestOptionalExtensions(nvidia, true, true, false);
  CHECK(r.present_id2);
  CHECK(r.present_wait2);
  CHECK(r.present_id);
  CHECK(r.present_wait);
}

TEST_CASE("present id without present wait is not requested") {
  DeviceOffer offer;
  offer.surface_capabilities2 = true;
  offer.present_id2 = true;
  offer.present_id = true;
  const DeviceRequest r = RequestOptionalExtensions(offer, true, true, false);
  CHECK_FALSE(r.present_id2);
  CHECK_FALSE(r.present_id);
}

TEST_CASE("a device without a swapchain asks for no present extensions") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, false, false);
  CHECK_FALSE(r.present_id2);
  CHECK_FALSE(r.present_wait2);
}

TEST_CASE("a device without GPU emulation asks for no pipeline library") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), false, true, false);
  CHECK_FALSE(r.pipeline_library);
  CHECK_FALSE(r.graphics_pipeline_library);
}

TEST_CASE("the owner's device enables the library, fast link, dynamic state and present-wait v2") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  const OptionalFeatures f = EnableOptionalFeatures(r, AmdFeatures(), true, false);
  CHECK(f.graphicsPipelineLibrary);
  CHECK(f.graphicsPipelineLibraryFastLinking);
  CHECK(f.graphicsPipelineLibraryIndependentInterpolationDecoration);
  CHECK(f.extendedDynamicState);
  CHECK(f.presentId2);
  CHECK(f.presentWait2);
  CHECK_FALSE(f.presentId);
  CHECK_FALSE(f.presentWait);
}

TEST_CASE("a listed extension whose feature bit is off stays off") {
  DeviceFeatureOffer features = AmdFeatures();
  features.graphicsPipelineLibrary = false;
  features.presentWait2 = false;
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  const OptionalFeatures f = EnableOptionalFeatures(r, features, true, false);
  CHECK_FALSE(f.graphicsPipelineLibrary);
  // Fast linking is a property of the library; without the library it means nothing.
  CHECK_FALSE(f.graphicsPipelineLibraryFastLinking);
  CHECK_FALSE(f.presentId2);
  CHECK_FALSE(f.presentWait2);
}

TEST_CASE("present-wait v1 is enabled beside v2, so a surface without v2 can fall back to it") {
  DeviceOffer nvidia = AmdOffer();
  nvidia.present_id = true;
  nvidia.present_wait = true;
  DeviceFeatureOffer features = AmdFeatures();
  features.presentId = true;
  features.presentWait = true;
  const DeviceRequest r = RequestOptionalExtensions(nvidia, true, true, false);
  const OptionalFeatures both = EnableOptionalFeatures(r, features, true, false);
  CHECK(both.presentWait2);
  CHECK(both.presentId);
  CHECK(both.presentWait);

  features.presentWait2 = false;
  const OptionalFeatures v1 = EnableOptionalFeatures(r, features, true, false);
  CHECK_FALSE(v1.presentWait2);
  CHECK(v1.presentId);
  CHECK(v1.presentWait);
}

TEST_CASE("dynamic state needs Vulkan 1.3 and is off under the developer switch") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  CHECK_FALSE(EnableOptionalFeatures(r, AmdFeatures(), false, false).extendedDynamicState);
  const DeviceRequest off = RequestOptionalExtensions(AmdOffer(), true, true, true);
  const OptionalFeatures f = EnableOptionalFeatures(off, AmdFeatures(), true, true);
  CHECK_FALSE(f.extendedDynamicState);
  CHECK_FALSE(f.graphicsPipelineLibrary);
  CHECK_FALSE(f.presentWait2);
}

TEST_CASE("the log names every optional feature as on or off") {
  const DeviceRequest r = RequestOptionalExtensions(AmdOffer(), true, true, false);
  const std::vector<std::string> lines =
      DescribeOptionalFeatures(EnableOptionalFeatures(r, AmdFeatures(), true, false));
  const std::vector<std::string> expected = {
      "graphicsPipelineLibrary: on",
      "graphicsPipelineLibraryFastLinking: on",
      "graphicsPipelineLibraryIndependentInterpolationDecoration: on",
      "extendedDynamicState: on",
      "presentWait2: on",
      "presentWait: off",
  };
  CHECK(lines == expected);
}
