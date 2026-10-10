// `wweallstars.exe --print-settings-schema`: writes the player settings list as
// JSON to standard output and exits before any window opens or the runtime
// starts (launcher spec, "Settings hand-off"). Also the headless modes' shared
// command line and output (settings/headless.h).

#include "settings/schema.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <rex/platform.h>

#include "settings/gpu_plugin_settings.h"
#include "settings/headless.h"
#include "settings/registry.h"

#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#endif

namespace wwe::settings {

std::vector<std::string> ProcessArguments() {
  std::vector<std::string> args;
#if REX_PLATFORM_WIN32
  // The switches are plain ASCII.
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  for (int i = 0; wide && i < count; ++i) {
    std::string arg;
    for (const wchar_t* c = wide[i]; *c; ++c) arg += *c < 0x80 ? static_cast<char>(*c) : '?';
    args.push_back(std::move(arg));
  }
  LocalFree(wide);
#endif
  return args;
}

void UseStandardOutput() {
#if REX_PLATFORM_WIN32
  // A GUI exe has no console: a pipe or file the caller gave already works,
  // otherwise print to the terminal that started it.
  if (_fileno(stdout) < 0 && AttachConsole(ATTACH_PARENT_PROCESS)) {
    std::freopen("CONOUT$", "w", stdout);
  }
  // Plain \n line ends, so the committed schema copy matches byte for byte.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void PrintAndExit(const std::string& text) {
  UseStandardOutput();
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
  std::exit(std::ferror(stdout) ? EXIT_FAILURE : EXIT_SUCCESS);
}

void PrintSchemaAndExitIfAsked() {
  if (SchemaRequested(ProcessArguments())) {
    LoadGpuPluginSettings();
    PrintAndExit(SchemaJson(WithLifecycles(PlayerSettings())));
  }
}

}  // namespace wwe::settings
