#pragma once

#include <string>

namespace ptoolkit
{
constexpr int kMajorVersion = 0;
constexpr int kMinorVersion = 1;
constexpr int kPatchVersion = 0;
constexpr const char* kAppName = "ptoolkit";

const std::string& GetAppVersion();
}  // namespace ptoolkit
