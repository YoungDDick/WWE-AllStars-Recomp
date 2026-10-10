// The game's headless modes for the launcher (--print-settings-schema,
// --list-gpus): each prints its data to standard output and exits before any
// window opens or the runtime starts.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wwe::settings {

inline bool HasSwitch(const std::vector<std::string>& args, std::string_view name) {
  for (const std::string& arg : args) {
    if (arg == name) return true;
  }
  return false;
}

// The process's command line (the SDK's entry point keeps argv to itself).
// Defined in schema_export.cpp (the game only).
std::vector<std::string> ProcessArguments();

// Makes standard output reach the caller (a pipe, a file or the terminal that
// started the game) with plain \n line ends. Defined in schema_export.cpp.
void UseStandardOutput();

// Writes text to standard output (a pipe, a file or the terminal that started
// the game) with plain \n line ends, then exits. Defined in schema_export.cpp.
[[noreturn]] void PrintAndExit(const std::string& text);

}  // namespace wwe::settings
