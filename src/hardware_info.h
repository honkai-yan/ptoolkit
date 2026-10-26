#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace hardware_info
{
#ifdef _WIN32
struct GpuInfo
{
  std::string brand_name;  // UTF-8 adapter description.
  std::uint64_t dedicated_memory_bytes;
};

// Full CPU brand string with leading/trailing padding removed.
std::expected<std::string, std::string> GetCpuBrandName();
// Total active physical cores across all processor packages and groups.
// Includes both performance and efficiency cores, not SMT threads.
std::expected<unsigned int, std::string> GetCpuCoreCount();
// Hardware GPUs, including integrated GPUs; software adapters excluded.
// Deduplicated by physical PnP hardware key, falling back to adapter LUID when
// unavailable. Identical model names and memory sizes are not merged.
// Dedicated video memory only, excluding shared system memory.
std::expected<std::vector<GpuInfo>, std::string> GetGpus();
// Physically installed RAM, including memory reserved by firmware/devices.
std::expected<std::uint64_t, std::string> GetTotalMemoryBytes();
// Sum of unique fixed volumes with drive letters, in bytes (not free space).
// Excludes hidden/unmounted volumes, unallocated space and removable/network
// drives. Windows per-user disk quotas, if enabled, can limit this value.
std::expected<std::uint64_t, std::string> GetTotalDiskBytes();
#endif
}  // namespace hardware_info
