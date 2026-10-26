#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include <ptoolkit_editor/document.h>

namespace ptoolkit
{
struct ClaudeConfig
{
  std::filesystem::path path;
  std::shared_ptr<editor::Document> document;
};
std::expected<std::filesystem::path, std::string> ClaudeSettingsPath();
std::expected<ClaudeConfig, std::string> LoadClaudeConfig(
    const std::filesystem::path& path);
}  // namespace ptoolkit
