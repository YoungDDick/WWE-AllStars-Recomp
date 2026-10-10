// Stable names for Native's passes, resolves and the present copy
// (low-end-preset tickets 01 and 04).

#include <catch_amalgamated.hpp>

#include "native/pass_names.h"

using wwe::native::FormatName;
using wwe::native::PassDescription;
using wwe::native::PassName;

TEST_CASE("a pass is named by its host size and format, and by sRGB and depth when used") {
  CHECK(PassName({1280, 720, VK_FORMAT_R8G8B8A8_UNORM, false, false}) == "pass 1280x720 RGBA8");
  CHECK(PassName({2560, 1440, VK_FORMAT_R8G8B8A8_UNORM, true, true}) ==
        "pass 2560x1440 RGBA8 sRGB +depth");
  CHECK(PassName({640, 360, VK_FORMAT_R16G16B16A16_SFLOAT, false, true}) ==
        "pass 640x360 RGBA16F +depth");
}

TEST_CASE("the same description always gives the same name") {
  const PassDescription pass{1024, 1024, VK_FORMAT_R32_SFLOAT, false, true};
  CHECK(PassName(pass) == PassName(pass));
  CHECK(PassName(pass) == "pass 1024x1024 R32F +depth");
}

TEST_CASE("every render-target format has a short name; others keep their number") {
  CHECK(FormatName(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == "RGB10A2");
  CHECK(FormatName(VK_FORMAT_R16G16_SFLOAT) == "RG16F");
  CHECK(FormatName(VK_FORMAT_R32G32_SFLOAT) == "RG32F");
  CHECK(FormatName(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) == "format 133");
}

TEST_CASE("resolves and the present copy say what they are") {
  CHECK(wwe::native::ResolveName(640, 360, VK_FORMAT_R8G8B8A8_UNORM, false) ==
        "resolve 640x360 RGBA8");
  CHECK(wwe::native::ResolveName(1280, 720, VK_FORMAT_D32_SFLOAT_S8_UINT, true) ==
        "resolve depth 1280x720");
  CHECK(wwe::native::PresentName(2560, 1440) == "present 2560x1440");
}
