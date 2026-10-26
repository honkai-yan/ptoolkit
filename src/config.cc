#include "config.h"

#include <format>

namespace ptoolkit
{
const std::string& GetAppVersion()
{
  static std::string version;
  if (version.empty())
  {
    version =
        std::format("v{}.{}.{}", kMajorVersion, kMinorVersion, kPatchVersion);
  }
  return version;
}
}  // namespace ptoolkit
