#include "system_info.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#pragma comment(lib, "Advapi32.lib")
#endif

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <cerrno>
#include <cstring>
#endif

namespace system_info
{
#ifdef __APPLE__
namespace
{
std::expected<std::string, std::string> ReadVersion(const char* key)
{
  size_t size = 0;
  if (sysctlbyname(key, nullptr, &size, nullptr, 0) || size == 0)
    return std::unexpected(std::string(key) + ": " + std::strerror(errno));
  std::string value(size, '\0');
  if (sysctlbyname(key, value.data(), &size, nullptr, 0))
    return std::unexpected(std::string(key) + ": " + std::strerror(errno));
  value.resize(size);
  if (!value.empty() && value.back() == '\0') value.pop_back();
  if (value.empty()) return std::unexpected(std::string(key) + " is empty");
  return value;
}
}  // namespace
#endif

std::expected<std::string, std::string> GetNameZh()
{
#ifdef _WIN32
  return windows_version::GetWindowsNameZh();
#elif defined(__APPLE__)
  return std::string("macOS");
#else
  return std::unexpected("Unsupported operating system");
#endif
}
std::expected<std::string, std::string> GetNameEn()
{
#ifdef _WIN32
  return windows_version::GetWindowsNameEn();
#else
  return GetNameZh();
#endif
}
std::expected<std::string, std::string> GetDisplayVersion()
{
#ifdef _WIN32
  return windows_version::GetWindowsDisplayVersion();
#elif defined(__APPLE__)
  return ReadVersion("kern.osproductversion");
#else
  return std::unexpected("Unsupported operating system");
#endif
}
std::expected<std::string, std::string> GetBuildVersion()
{
#ifdef _WIN32
  return windows_version::GetWindowsBuildVersion();
#elif defined(__APPLE__)
  return ReadVersion("kern.osversion");
#else
  return std::unexpected("Unsupported operating system");
#endif
}
#ifdef _WIN32
namespace windows_version
{
namespace
{
[[noreturn]] void ThrowWin32(LSTATUS error, const char* operation)
{
  // ASCII is valid UTF-8; avoid locale-dependent system_error::what() encoding.
  throw std::runtime_error(std::string(operation) + ": Win32 error " +
                           std::to_string(error));
}

// Implementation exceptions are converted to string errors at the API boundary.
template <class F>
std::expected<std::string, std::string> ToExpected(F&& query)
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

// Windows APIs use UTF-16 internally; public interfaces return UTF-8.
std::string ToUtf8(const std::wstring& text)
{
  if (text.empty()) return {};
  if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
  {
    throw std::length_error("UTF-16 string too long");
  }
  const int length = static_cast<int>(text.size());
  const int bytes =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                          nullptr, 0, nullptr, nullptr);
  if (bytes == 0)
  {
    ThrowWin32(GetLastError(), "WideCharToMultiByte(size)");
  }
  std::string result(bytes, '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                          result.data(), bytes, nullptr, nullptr) == 0)
  {
    ThrowWin32(GetLastError(), "WideCharToMultiByte(convert)");
  }
  return result;
}

class CurrentVersionKey
{
public:
  CurrentVersionKey()
  {
    const auto error = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key_);
    if (error != ERROR_SUCCESS) ThrowWin32(error, "RegOpenKeyExW");
  }
  ~CurrentVersionKey() { RegCloseKey(key_); }
  CurrentVersionKey(const CurrentVersionKey&) = delete;
  CurrentVersionKey& operator=(const CurrentVersionKey&) = delete;
  std::wstring ReadString(const wchar_t* name) const
  {
    wchar_t buffer[256]{};
    DWORD bytes = sizeof(buffer);
    const auto error = RegGetValueW(key_, nullptr, name, RRF_RT_REG_SZ, nullptr,
                                    buffer, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return {};
    if (error != ERROR_SUCCESS) ThrowWin32(error, "RegGetValueW(REG_SZ)");
    return buffer;
  }
  std::optional<DWORD> ReadDword(const wchar_t* name) const
  {
    DWORD value = 0, bytes = sizeof(value);
    const auto error = RegGetValueW(key_, nullptr, name, RRF_RT_REG_DWORD,
                                    nullptr, &value, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (error != ERROR_SUCCESS) ThrowWin32(error, "RegGetValueW(REG_DWORD)");
    return value;
  }

private:
  HKEY key_ = nullptr;
};

OSVERSIONINFOEXW QueryVersion()
{
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
  const HMODULE module = GetModuleHandleW(L"ntdll.dll");
  if (!module) ThrowWin32(GetLastError(), "GetModuleHandleW(ntdll)");
  const auto fn = reinterpret_cast<RtlGetVersionFn>(
      GetProcAddress(module, "RtlGetVersion"));
  if (!fn) ThrowWin32(GetLastError(), "GetProcAddress(RtlGetVersion)");
  OSVERSIONINFOEXW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (fn(&version) != 0) throw std::runtime_error("RtlGetVersion failed");
  if (version.wProductType != VER_NT_WORKSTATION ||
      version.dwMajorVersion != 10 || version.dwMinorVersion != 0)
    throw std::runtime_error(
        "Only Windows 10 / 11 client editions are supported");
  return version;
}

std::wstring GetFamily(const OSVERSIONINFOEXW& version)
{
  // Existing Windows 10 / 11 clients share NT version 10.0.
  return version.dwBuildNumber >= 22000 ? L"Windows 11" : L"Windows 10";
}

struct Edition
{
  DWORD sku;
  const wchar_t* zh;
  const wchar_t* en;
};

// Immutable lookup data, not a cache or mutable shared state.
constexpr Edition editions[] = {
    {PRODUCT_CORE, L"家庭版", L"Home"},
    {PRODUCT_CORE_N, L"家庭版 N", L"Home N"},
    {PRODUCT_CORE_SINGLELANGUAGE, L"家庭单语言版", L"Home Single Language"},
    {PRODUCT_CORE_COUNTRYSPECIFIC, L"家庭中文版", L"Home China"},
    {PRODUCT_PROFESSIONAL, L"专业版", L"Pro"},
    {PRODUCT_PROFESSIONAL_N, L"专业版 N", L"Pro N"},
    {PRODUCT_PRO_WORKSTATION, L"专业工作站版", L"Pro for Workstations"},
    {PRODUCT_PRO_WORKSTATION_N, L"专业工作站版 N", L"Pro for Workstations N"},
    {PRODUCT_PRO_FOR_EDUCATION, L"专业教育版", L"Pro Education"},
    {PRODUCT_PRO_FOR_EDUCATION_N, L"专业教育版 N", L"Pro Education N"},
    {PRODUCT_EDUCATION, L"教育版", L"Education"},
    {PRODUCT_EDUCATION_N, L"教育版 N", L"Education N"},
    {PRODUCT_ENTERPRISE, L"企业版", L"Enterprise"},
    {PRODUCT_ENTERPRISE_N, L"企业版 N", L"Enterprise N"},
};

std::string QueryName(bool chinese)
{
  const auto version = QueryVersion();
  const auto family = GetFamily(version);
  DWORD sku = PRODUCT_UNDEFINED;
  if (!GetProductInfo(version.dwMajorVersion, version.dwMinorVersion,
                      version.wServicePackMajor, version.wServicePackMinor,
                      &sku))
  {
    throw std::runtime_error("GetProductInfo failed");
  }
  std::wstring editionZh, editionEn;
  for (const auto& edition : editions)
  {
    if (edition.sku == sku)
    {
      editionZh = edition.zh;
      editionEn = edition.en;
      break;
    }
  }
  if (editionEn.empty())
  {
    const CurrentVersionKey registry;
    auto id = registry.ReadString(L"EditionID");
    if (id.empty()) id = L"SKU=" + std::to_wstring(sku);
    editionZh = L"未知版本 [" + id + L"]";
    editionEn = L"Unknown edition [" + id + L"]";
  }
  return ToUtf8(family + L" " + (chinese ? editionZh : editionEn));
}
}  // namespace

// Every call queries the OS independently. No initialization, instance or
// cache. Query errors return unexpected; missing release labels return an empty
// string.
std::expected<std::string, std::string> GetWindowsNameZh()
{
  return ToExpected([] { return QueryName(true); });
}

std::expected<std::string, std::string> GetWindowsNameEn()
{
  return ToExpected([] { return QueryName(false); });
}

std::expected<std::string, std::string> GetWindowsDisplayVersion()
{
  return ToExpected(
      []
      {
        (void)QueryVersion();  // Validate the supported OS range.
        const CurrentVersionKey registry;
        auto label = registry.ReadString(L"DisplayVersion");
        if (label.empty()) label = registry.ReadString(L"ReleaseId");
        return ToUtf8(label);
      });
}

std::expected<std::string, std::string> GetWindowsBuildVersion()
{
  return ToExpected(
      []
      {
        const auto version = QueryVersion();
        const CurrentVersionKey registry;
        auto build = std::to_string(version.dwBuildNumber);
        // Do not invent a revision if UBR is missing.
        if (const auto ubr = registry.ReadDword(L"UBR"))
          build += "." + std::to_string(*ubr);
        return build;
      });
}
}  // namespace windows_version
#endif
}  // namespace system_info
