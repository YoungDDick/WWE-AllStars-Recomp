// The rules a text setting's value follows (launcher-online spec, "A text
// setting kind"). The launcher applies the same rules from the schema export
// (Launcher.Core TextRules), so a value it saves is one the game accepts.

#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include "settings/page_model.h"

namespace wwe::settings {

// The rule's name in the schema export; empty for none.
inline const char* TextRuleName(TextRule rule) {
  switch (rule) {
    case TextRule::kGamertag: return "gamertag";
    case TextRule::kServerAddress: return "server_address";
    default: return "";
  }
}

namespace text_rules_detail {

inline bool Letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool Digit(char c) { return c >= '0' && c <= '9'; }

// host or host:port, the port 1 to 65535; a host is letters, digits, dots and
// hyphens.
inline bool HostAndPort(std::string_view text) {
  const size_t colon = text.find(':');
  const std::string_view host = text.substr(0, colon);
  if (host.empty()) return false;
  for (char c : host) {
    if (!Letter(c) && !Digit(c) && c != '.' && c != '-') return false;
  }
  if (colon == std::string_view::npos) return true;
  const std::string_view port = text.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  unsigned value = 0;
  for (char c : port) {
    if (!Digit(c)) return false;
    value = value * 10 + unsigned(c - '0');
  }
  return value >= 1 && value <= 65535;
}

inline bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
  }
  return true;
}

}  // namespace text_rules_detail

// Why the value breaks the rule, as the launcher and the log say it; empty if
// it follows it.
inline std::string TextRuleError(TextRule rule, std::string_view value) {
  using namespace text_rules_detail;
  if (rule == TextRule::kGamertag) {
    // Xbox LIVE's gamertags: 1 to 15 characters, letters, digits and spaces,
    // first a letter.
    if (value.empty()) return "Type a player name.";
    if (value.size() > 15) return "A player name has at most 15 characters.";
    if (!Letter(value.front())) return "A player name starts with a letter.";
    for (char c : value) {
      if (!Letter(c) && !Digit(c) && c != ' ') {
        return "A player name has only letters, digits and spaces.";
      }
    }
    if (value.back() == ' ') return "A player name does not end with a space.";
  } else if (rule == TextRule::kServerAddress) {
    // What the SDK's ApiBaseUrl turns into the API's base URL: a bare host gets
    // http and port 36000, a URL is kept.
    if (value.empty()) return "Type a game server address.";
    for (char c : value) {
      if (std::isspace(static_cast<unsigned char>(c))) return "A server address has no spaces.";
    }
    std::string_view authority = value;
    if (const size_t scheme = value.find("://"); scheme != std::string_view::npos) {
      if (!StartsWithNoCase(value, "http://") && !StartsWithNoCase(value, "https://")) {
        return "A server URL starts with http:// or https://.";
      }
      authority = value.substr(scheme + 3);
      authority = authority.substr(0, authority.find('/'));
    }
    if (!HostAndPort(authority)) {
      return "A server address is a host name, host:port (port 1 to 65535), or an http(s) URL.";
    }
  }
  return {};
}

}  // namespace wwe::settings
