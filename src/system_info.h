#pragma once

#include <expected>
#include <string>

namespace system_info
{
#ifdef _WIN32
namespace windows_version
{
// UTF-8 strings on success and failure. Each call queries the OS independently.
std::expected<std::string, std::string> GetWindowsNameZh();
std::expected<std::string, std::string> GetWindowsNameEn();
std::expected<std::string, std::string> GetWindowsDisplayVersion();
std::expected<std::string, std::string> GetWindowsBuildVersion();
}  // namespace windows_version
#endif
}  // namespace system_info
