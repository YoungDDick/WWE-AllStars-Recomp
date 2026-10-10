// Saving one setting into the flat TOML config (wweallstars.toml) without
// touching the rest of the file, unlike the SDK's SaveConfig, which rewrites
// every non-default cvar (debug and command-line values included).

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <rex/cvar.h>
#include <toml++/toml.hpp>

namespace wwe::settings {

// The page's own settings file, in the user data folder so that replacing the
// game folder keeps it (launcher ticket 21). Loaded after the main config, so
// the page's settings apply whatever state that file is in (in-game settings
// ticket 10).
inline std::filesystem::path SettingsPath(const std::filesystem::path& user_data_root) {
  return user_data_root / "wweallstars.settings.toml";
}

// Why a config file does not parse; empty if it parses or does not exist.
// The SDK loads the main config before logging starts, so its own error
// message is lost.
inline std::string ConfigError(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return {};
  }
  try {
    toml::parse_file(path.string());
  } catch (const toml::parse_error& parse_error) {
    return std::string(parse_error.description()) + " (line " +
           std::to_string(parse_error.source().begin.line) + ")";
  }
  return {};
}

// The value as a TOML literal for the cvar: strings quoted with backslashes
// and quotes escaped, numbers and booleans bare.
inline std::string TomlLiteral(const std::string& name, const std::string& value) {
  const rex::cvar::FlagEntry* flag = rex::cvar::GetFlagInfo(name);
  if (!flag || flag->type != rex::cvar::FlagType::String) {
    return value;
  }
  std::string quoted = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') quoted += '\\';
    quoted += c;
  }
  return quoted + "\"";
}

// The config text with key set to the TOML literal value: its line replaced
// (keeping the line end), or appended if the file has none.
inline std::string SetTomlKey(std::string_view text, std::string_view key,
                              std::string_view value) {
  const std::string line = std::string(key) + " = " + std::string(value);
  std::string out;
  bool found = false;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    end = end == std::string_view::npos ? text.size() : end + 1;
    std::string_view current = text.substr(start, end - start);
    std::string_view ending;
    if (current.ends_with("\r\n")) {
      ending = "\r\n";
    } else if (current.ends_with('\n')) {
      ending = "\n";
    }
    std::string_view body = current.substr(0, current.size() - ending.size());
    const size_t first = body.find_first_not_of(" \t");
    std::string_view rest = first == std::string_view::npos ? "" : body.substr(first);
    bool match = false;
    if (!found && rest.starts_with(key)) {
      std::string_view after = rest.substr(key.size());
      const size_t equals = after.find_first_not_of(" \t");
      match = equals != std::string_view::npos && after[equals] == '=';
    }
    if (match) {
      out += line;
      out += ending;
      found = true;
    } else {
      out += current;
    }
    start = end;
  }
  if (!found) {
    if (!out.empty() && out.back() != '\n') {
      out += '\n';
    }
    out += line;
    out += '\n';
  }
  return out;
}

// Writes key = value into the config file through a temporary file and a
// rename, so a crash never leaves it half written.
inline bool SaveSetting(const std::filesystem::path& path, std::string_view key,
                        std::string_view value) {
  std::string text;
  if (std::ifstream in{path, std::ios::binary}) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
    if (!out) {
      return false;
    }
    out << SetTomlKey(text, key, value);
    if (!out) {
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

}  // namespace wwe::settings
