#include "hardware_info.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>  // Defines NTSTATUS required by d3dkmthk.h.

#include <d3dkmthk.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif

#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hardware_info
{
namespace
{
[[noreturn]] void ThrowWin32(const char* operation)
{
  const auto error = GetLastError();
  throw std::runtime_error(std::string(operation) + ": Win32 error " +
                           std::to_string(error));
}

void CheckHresult(HRESULT result, const char* operation)
{
  if (FAILED(result))
    throw std::runtime_error(
        std::string(operation) + ": HRESULT " +
        std::to_string(static_cast<unsigned long>(result)));
}

template <class T, class F>
std::expected<T, std::string> ToExpected(F&& query)
{
  try
  {
    return std::forward<F>(query)();
  }
  catch (const std::exception& error)
  {
    return std::unexpected(std::string(error.what()));
  }
}

std::string ToUtf8(const wchar_t* text)
{
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                        nullptr, 0, nullptr, nullptr);
  if (bytes == 0) ThrowWin32("WideCharToMultiByte(size)");
  std::string result(bytes, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                           result.data(), bytes, nullptr, nullptr))
    ThrowWin32("WideCharToMultiByte(convert)");
  result.pop_back();  // Drop the converted terminating null.
  return result;
}

// Display-only/virtual DXGI entries can have different LUIDs and adapter GUIDs
// while referring to the same physical device's PnP hardware registry key.
std::optional<std::wstring> GetGpuPnpKey(const LUID& luid)
{
  D3DKMT_OPENADAPTERFROMLUID open{};
  open.AdapterLuid = luid;
  if (D3DKMTOpenAdapterFromLuid(&open) < 0) return std::nullopt;
  struct AdapterGuard
  {
    D3DKMT_HANDLE handle;
    ~AdapterGuard()
    {
      D3DKMT_CLOSEADAPTER close{handle};
      (void)D3DKMTCloseAdapter(&close);
    }
  } guard{open.hAdapter};

  std::vector<wchar_t> buffer(1024);
  UINT length = static_cast<UINT>(buffer.size());
  D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY pnp{0, D3DKMT_PNP_KEY_HARDWARE,
                                            buffer.data(), &length};
  D3DKMT_QUERYADAPTERINFO query{open.hAdapter, KMTQAITYPE_PHYSICALADAPTERPNPKEY,
                                &pnp, sizeof(pnp)};
  auto status = D3DKMTQueryAdapterInfo(&query);
  if (status < 0 && length > buffer.size())
  {
    buffer.resize(length);
    pnp.pDest = buffer.data();
    status = D3DKMTQueryAdapterInfo(&query);
  }
  if (status < 0 || length == 0 || length > buffer.size()) return std::nullopt;
  // Require a nonempty, terminated key before using it as an identity.
  if (buffer[0] == L'\0' || buffer[length - 1] != L'\0') return std::nullopt;
  std::wstring key(buffer.data(), length - 1);
  // Registry device paths are case insensitive ASCII identifiers.
  for (auto& character : key)
    if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
  return key;
}
}  // namespace

std::expected<std::string, std::string> GetCpuBrandName()
{
#if defined(_M_X64) || defined(_M_IX86)
  int registers[4]{};
  __cpuid(registers, static_cast<int>(0x80000000u));
  if (static_cast<unsigned int>(registers[0]) < 0x80000004u)
    return std::unexpected("CPU brand string is not supported");

  char brand[49]{};
  for (unsigned int i = 0; i < 3; ++i)
  {
    __cpuid(registers, static_cast<int>(0x80000002u + i));
    std::memcpy(brand + i * 16, registers, 16);
  }
  std::string result(brand);
  const auto first = result.find_first_not_of(' ');
  if (first == std::string::npos)
    return std::unexpected("CPU brand string is empty");
  return result.substr(first, result.find_last_not_of(' ') - first + 1);
#else
  return std::unexpected("CPU brand query requires an x86 or x64 processor");
#endif
}

std::expected<unsigned int, std::string> GetCpuCoreCount()
{
  try
  {
    DWORD bytes = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr,
                                         &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
      return std::unexpected(
          "GetLogicalProcessorInformationEx(size): Win32 error " +
          std::to_string(GetLastError()));

    std::vector<std::byte> buffer(bytes);
    for (;;)
    {
      auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
          buffer.data());
      bytes = static_cast<DWORD>(buffer.size());
      if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bytes))
        break;
      const auto error = GetLastError();
      if (error != ERROR_INSUFFICIENT_BUFFER || bytes <= buffer.size())
        return std::unexpected(
            "GetLogicalProcessorInformationEx: Win32 error " +
            std::to_string(error));
      buffer.resize(bytes);
    }

    unsigned int cores = 0;
    for (size_t offset = 0; offset < bytes;)
    {
      constexpr auto header_size =
          offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
      if (bytes - offset < header_size)
        return std::unexpected("CPU topology record is truncated");
      const auto* info =
          reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
              buffer.data() + offset);
      if (info->Size < header_size || info->Size > bytes - offset)
        return std::unexpected("CPU topology record has an invalid size");
      if (info->Relationship == RelationProcessorCore) ++cores;
      offset += info->Size;
    }
    if (cores == 0)
      return std::unexpected("CPU topology contains no physical cores");
    return cores;
  }
  catch (const std::exception& error)
  {
    return std::unexpected(std::string(error.what()));
  }
}

std::expected<std::vector<GpuInfo>, std::string> GetGpus()
{
  return ToExpected<std::vector<GpuInfo>>(
      []
      {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        CheckHresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),
                     "CreateDXGIFactory1");
        std::vector<GpuInfo> gpus;
        std::set<std::wstring> physical_devices;
        std::set<std::pair<LONG, DWORD>> adapter_ids;
        for (UINT index = 0;; ++index)
        {
          Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
          const auto result =
              factory->EnumAdapters1(index, adapter.GetAddressOf());
          if (result == DXGI_ERROR_NOT_FOUND) break;
          CheckHresult(result, "EnumAdapters1");
          DXGI_ADAPTER_DESC1 description{};
          CheckHresult(adapter->GetDesc1(&description),
                       "IDXGIAdapter1::GetDesc1");
          if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
          if (!adapter_ids
                   .emplace(description.AdapterLuid.HighPart,
                            description.AdapterLuid.LowPart)
                   .second)
            continue;
          if (const auto key = GetGpuPnpKey(description.AdapterLuid))
          {
            if (!physical_devices.insert(*key).second) continue;
          }
          auto name = ToUtf8(description.Description);
          const auto memory =
              static_cast<std::uint64_t>(description.DedicatedVideoMemory);
          gpus.push_back({std::move(name), memory});
        }
        return gpus;
      });
}

std::expected<std::uint64_t, std::string> GetTotalMemoryBytes()
{
  return ToExpected<std::uint64_t>(
      []
      {
        ULONGLONG kilobytes = 0;
        if (!GetPhysicallyInstalledSystemMemory(&kilobytes))
          ThrowWin32("GetPhysicallyInstalledSystemMemory");
        if (kilobytes > (std::numeric_limits<std::uint64_t>::max)() / 1024)
          throw std::overflow_error("Installed memory size overflow");
        return static_cast<std::uint64_t>(kilobytes) * 1024;
      });
}

std::expected<std::uint64_t, std::string> GetTotalDiskBytes()
{
  return ToExpected<std::uint64_t>(
      []
      {
        const DWORD drives = GetLogicalDrives();
        if (drives == 0) ThrowWin32("GetLogicalDrives");
        std::set<std::wstring> volumes;
        std::uint64_t total = 0;
        for (unsigned int i = 0; i < 26; ++i)
        {
          if ((drives & (1u << i)) == 0) continue;
          wchar_t root[] = L"A:\\";
          root[0] += static_cast<wchar_t>(i);
          const auto type = GetDriveTypeW(root);
          if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR)
            throw std::runtime_error("Cannot determine drive type for " +
                                     ToUtf8(root));
          if (type != DRIVE_FIXED) continue;

          wchar_t volume[50]{};
          if (!GetVolumeNameForVolumeMountPointW(root, volume, 50))
            ThrowWin32("GetVolumeNameForVolumeMountPointW");
          if (!volumes.insert(volume).second) continue;

          ULARGE_INTEGER bytes{};
          if (!GetDiskFreeSpaceExW(root, nullptr, &bytes, nullptr))
            ThrowWin32("GetDiskFreeSpaceExW");
          if (bytes.QuadPart >
              (std::numeric_limits<std::uint64_t>::max)() - total)
            throw std::overflow_error("Total disk size overflow");
          total += bytes.QuadPart;
        }
        return total;
      });
}
}  // namespace hardware_info
#endif
