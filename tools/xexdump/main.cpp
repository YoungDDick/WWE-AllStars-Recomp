// Loads <dir>/default.xex the way rexglue codegen does (Runtime::LoadXexImage,
// which applies a sibling default.xexp) and writes the guest memory image to
// <out>: file offset = address - image base.
#include <cstdio>
#include <string>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

using rex::X_STATUS;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xexdump <dir> <out>\n");
    return 2;
  }
  rex::Runtime runtime(argv[1]);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                       .tool_mode = true}) != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  auto status = runtime.LoadXexImage("game:\\default.xex");
  if (status != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "load failed %08X\n", status);
    return 1;
  }
  auto* module = runtime.kernel_state()->GetExecutableModule()->xex_module();
  uint32_t base = module->base_address(), size = module->image_size();
  std::printf("base %08X size %08X entry %08X\n", base, size, module->entry_point());
  FILE* out = std::fopen(argv[2], "wb");
  if (!out) {
    std::fprintf(stderr, "cannot write %s\n", argv[2]);
    return 1;
  }
  bool written = std::fwrite(runtime.memory()->TranslateVirtual(base), 1, size, out) == size;
  if (std::fclose(out) != 0 || !written) {
    std::fprintf(stderr, "cannot write %s\n", argv[2]);
    return 1;
  }
  return 0;
}
