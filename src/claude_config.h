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
// The global prompt is plain text. A missing file opens as an empty document
// so it can be created with the normal save shortcut.
struct ClaudePrompt
{
  std::filesystem::path path;
  std::shared_ptr<editor::Document> document;
};
std::expected<std::filesystem::path, std::string> ClaudeSettingsPath();
std::expected<std::filesystem::path, std::string> ClaudePromptPath();
std::expected<ClaudeConfig, std::string> LoadClaudeConfig(
    const std::filesystem::path& path);
std::expected<ClaudePrompt, std::string> LoadClaudePrompt(
    const std::filesystem::path& path);
}  // namespace ptoolkit
