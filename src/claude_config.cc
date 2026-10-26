#include "claude_config.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace ptoolkit
{
std::expected<std::filesystem::path, std::string> ClaudeSettingsPath()
{
#ifdef _WIN32
  const DWORD size = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
  if (!size) return std::unexpected("USERPROFILE is unavailable");
  std::wstring home(size, L'\0');
  const auto copied =
      GetEnvironmentVariableW(L"USERPROFILE", home.data(), size);
  if (!copied || copied >= size)
    return std::unexpected("Cannot resolve USERPROFILE");
  home.resize(copied);
  return std::filesystem::path(home) / ".claude" / "settings.json";
#else
  const auto* home = std::getenv("HOME");
  if (!home || !*home) return std::unexpected("HOME is unavailable");
  return std::filesystem::path(home) / ".claude" / "settings.json";
#endif
}

std::expected<ClaudeConfig, std::string> LoadClaudeConfig(
    const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::unexpected(
        "Cannot open settings.json (file missing or unreadable)");
  std::string text{std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>()};
  if (file.bad()) return std::unexpected("Cannot read settings.json");
  if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
  try
  {
    const auto parsed = nlohmann::json::parse(text);
    if (!parsed.is_object())
      return std::unexpected("settings.json must contain a JSON object");
  }
  catch (const nlohmann::json::parse_error& error)
  {
    // Avoid JSON excerpts: settings may contain API keys or other secrets.
    return std::unexpected("Invalid JSON near byte " +
                           std::to_string(error.byte));
  }
  catch (const nlohmann::json::exception&)
  {
    return std::unexpected(
        "JSON contains an unsupported or out-of-range value");
  }
  std::string indent = "  ";
  auto line = text.find('\n');
  while (line != std::string::npos && line + 1 < text.size())
  {
    const auto first = text.find_first_not_of(" \t\r", line + 1);
    if (first != std::string::npos && first > line + 1 && text[first] != '\n')
    {
      indent = text.substr(line + 1, first - line - 1);
      if (indent.back() == '\r') indent.pop_back();
      break;
    }
    line = text.find('\n', line + 1);
  }
  // Validate first, then retain the original formatting and property order.
  return ClaudeConfig{path, std::make_shared<editor::Document>(text, indent)};
}
}  // namespace ptoolkit
