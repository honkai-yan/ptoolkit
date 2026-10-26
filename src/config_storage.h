#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ptoolkit
{
std::expected<std::string, std::string> ReadConfigText(
    const std::filesystem::path& path);
// A fully written temporary file is published by rename. overwrite=false must
// never replace a pre-existing file, including when another process creates it.
std::expected<void, std::string> WriteConfigText(
    const std::filesystem::path& path, std::string_view text,
    bool overwrite = true);
std::expected<std::filesystem::path, std::string> ClaudePresetsPath();

class PresetStore
{
public:
  explicit PresetStore(std::filesystem::path directory)
      : m_directory(std::move(directory))
  {
  }
  std::expected<std::vector<std::string>, std::string> List() const;
  std::expected<void, std::string> Create(std::string_view name,
                                          std::string_view text) const;
  std::expected<std::string, std::string> Load(std::string_view name) const;
  std::expected<void, std::string> Rename(std::string_view name,
                                          std::string_view replacement) const;
  std::expected<void, std::string> Delete(std::string_view name) const;

private:
  std::expected<std::filesystem::path, std::string> Path(
      std::string_view name) const;
  std::filesystem::path m_directory;
};
}  // namespace ptoolkit
