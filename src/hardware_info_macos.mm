#include "hardware_info.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <sys/sysctl.h>

#include <cerrno>
#include <cstring>
#include <limits>

namespace hardware_info
{
namespace
{
template <class T>
std::expected<T, std::string> ReadNumber(const char* key)
{
  T value{};
  size_t size = sizeof(value);
  if (sysctlbyname(key, &value, &size, nullptr, 0))
    return std::unexpected(std::string(key) + ": " + std::strerror(errno));
  if (size != sizeof(value) || value == 0)
    return std::unexpected(std::string(key) + ": invalid value");
  return value;
}

template <class T>
struct CfGuard
{
  T value;
  ~CfGuard()
  {
    if (value) CFRelease(value);
  }
};
struct IoGuard
{
  io_object_t value;
  ~IoGuard()
  {
    if (value) IOObjectRelease(value);
  }
};
}  // namespace

std::expected<std::string, std::string> GetCpuBrandName()
{
  const char* key = "machdep.cpu.brand_string";
  size_t size = 0;
  if (sysctlbyname(key, nullptr, &size, nullptr, 0) || size == 0)
    return std::unexpected(std::string(key) + ": " + std::strerror(errno));
  std::string brand(size, '\0');
  if (sysctlbyname(key, brand.data(), &size, nullptr, 0))
    return std::unexpected(std::string(key) + ": " + std::strerror(errno));
  brand.resize(size);
  if (!brand.empty() && brand.back() == '\0') brand.pop_back();
  const auto first = brand.find_first_not_of(' ');
  if (first == std::string::npos) return std::unexpected("CPU brand is empty");
  return brand.substr(first, brand.find_last_not_of(' ') - first + 1);
}

std::expected<unsigned int, std::string> GetCpuCoreCount()
{
  return ReadNumber<unsigned int>("hw.physicalcpu");
}

std::expected<std::uint64_t, std::string> GetTotalMemoryBytes()
{
  return ReadNumber<std::uint64_t>("hw.memsize");
}

std::expected<std::vector<GpuInfo>, std::string> GetGpus()
{
  @autoreleasepool
  {
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    if (!devices) return std::unexpected("MTLCopyAllDevices failed");
    std::vector<GpuInfo> result;
    for (id<MTLDevice> device in devices)
    {
      const char* name = device.name.UTF8String;
      if (!name || !*name) return std::unexpected("Metal GPU name is empty");
      // recommendedMaxWorkingSetSize is a budget, not dedicated VRAM.
      result.push_back({name, 0});
      if (!device.hasUnifiedMemory)
      {
        // Query actual VRAM through the device's IOKit registry entry.
        IoGuard entry{IOServiceGetMatchingService(
            kIOMainPortDefault, IORegistryEntryIDMatching(device.registryID))};
        if (!entry.value) return std::unexpected("Cannot find GPU registry entry");
        CfGuard<CFTypeRef> memory{IORegistryEntrySearchCFProperty(
            entry.value, kIOServicePlane, CFSTR("VRAM,totalMB"),
            kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents)};
        std::int64_t megabytes = 0;
        if (!memory.value || CFGetTypeID(memory.value) != CFNumberGetTypeID() ||
            !CFNumberGetValue(static_cast<CFNumberRef>(memory.value),
                              kCFNumberSInt64Type, &megabytes) ||
            megabytes <= 0 ||
            static_cast<std::uint64_t>(megabytes) >
                std::numeric_limits<std::uint64_t>::max() / (1024 * 1024))
          return std::unexpected("Dedicated GPU VRAM size is unavailable");
        result.back().dedicated_memory_bytes =
            static_cast<std::uint64_t>(megabytes) * 1024 * 1024;
      }
    }
    return result;
  }
}

std::expected<std::uint64_t, std::string> GetTotalDiskBytes()
{
  CfGuard<DASessionRef> session{DASessionCreate(kCFAllocatorDefault)};
  if (!session.value) return std::unexpected("DASessionCreate failed");
  auto* matching = IOServiceMatching(kIOMediaClass);
  if (!matching) return std::unexpected("IOServiceMatching(IOMedia) failed");
  CFDictionarySetValue(matching, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);
  io_iterator_t iterator = 0;
  const auto status =
      IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
  if (status != KERN_SUCCESS)
    return std::unexpected("IOServiceGetMatchingServices: " +
                           std::to_string(status));
  IoGuard iterator_guard{iterator};
  std::uint64_t total = 0;
  while (const auto media = IOIteratorNext(iterator))
  {
    IoGuard media_guard{media};
    // APFS synthesized whole media inherit the physical device's protocol and
    // internal flag. Only media directly served by a block-storage driver are
    // physical disks; synthesized containers sit below APFS container schemes.
    io_registry_entry_t parent = 0;
    if (IORegistryEntryGetParentEntry(media, kIOServicePlane, &parent) !=
        KERN_SUCCESS)
      return std::unexpected("Cannot inspect disk's storage driver");
    IoGuard parent_guard{parent};
    if (!IOObjectConformsTo(parent, "IOBlockStorageDriver")) continue;
    CfGuard<DADiskRef> disk{
        DADiskCreateFromIOMedia(kCFAllocatorDefault, session.value, media)};
    if (!disk.value) return std::unexpected("DADiskCreateFromIOMedia failed");
    CfGuard<CFDictionaryRef> description{DADiskCopyDescription(disk.value)};
    if (!description.value) return std::unexpected("DADiskCopyDescription failed");
    const auto get = [&](CFStringRef key)
    {
      return CFDictionaryGetValue(description.value, key);
    };
    // Exclude external devices and disk images.
    const auto internal = get(kDADiskDescriptionDeviceInternalKey);
    const auto removable = get(kDADiskDescriptionMediaRemovableKey);
    const auto physical = get(kDADiskDescriptionDeviceProtocolKey);
    if (!internal || !CFEqual(internal, kCFBooleanTrue) ||
        (removable && CFEqual(removable, kCFBooleanTrue)) ||
        !physical || CFEqual(physical, CFSTR("Virtual Interface")))
      continue;
    const auto size = get(kDADiskDescriptionMediaSizeKey);
    std::int64_t bytes = 0;
    if (!size || CFGetTypeID(size) != CFNumberGetTypeID() ||
        !CFNumberGetValue(static_cast<CFNumberRef>(size), kCFNumberSInt64Type,
                          &bytes) ||
        bytes <= 0)
      return std::unexpected("Internal disk has an invalid capacity");
    if (static_cast<std::uint64_t>(bytes) >
        std::numeric_limits<std::uint64_t>::max() - total)
      return std::unexpected("Total disk capacity overflow");
    total += static_cast<std::uint64_t>(bytes);
  }
  if (!total) return std::unexpected("No internal physical disks found");
  return total;
}
}  // namespace hardware_info
