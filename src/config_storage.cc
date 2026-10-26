#include "config_storage.h"
#include "claude_config.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace ptoolkit
{
namespace
{
std::string Error(std::string_view action, unsigned long code)
{
  return std::string(action) + " failed (system error " + std::to_string(code) +
         ")";
}
std::expected<void, std::string> Publish(const std::filesystem::path& from,
                                         const std::filesystem::path& to,
                                         bool overwrite)
{
#ifdef _WIN32
  if (!MoveFileExW(
          from.c_str(), to.c_str(),
          MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
  {
    const auto error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
      return std::unexpected("A preset with this name already exists");
    return std::unexpected(Error("Publishing file", error));
  }
#else
  if (overwrite)
  {
    if (::rename(from.c_str(), to.c_str()))
      return std::unexpected(Error("Publishing file", errno));
  }
  else
  {
    if (::link(from.c_str(), to.c_str()))
      return std::unexpected(errno == EEXIST
                                 ? "A preset with this name already exists"
                                 : Error("Publishing file", errno));
    ::unlink(from.c_str());
  }
#endif
  return {};
}
}  // namespace

std::expected<std::string, std::string> ReadConfigText(
    const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::unexpected("Cannot open configuration file");
  std::string text{std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>()};
  if (file.bad()) return std::unexpected("Cannot read configuration file");
  if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
  return text;
}

std::expected<void, std::string> WriteConfigText(
    const std::filesystem::path& path, std::string_view text, bool overwrite)
{
  static std::atomic<unsigned long long> sequence = 0;
  const auto temporary =
      path.parent_path() /
      (".ptoolkit-write-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       "-" + std::to_string(sequence++) + ".tmp");
  struct Cleanup
  {
    std::filesystem::path path;
    ~Cleanup()
    {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  };
#ifdef _WIN32
  const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return std::unexpected(Error("Opening temporary file", GetLastError()));
  Cleanup cleanup{temporary};
  DWORD failure = 0;
  for (std::size_t at = 0; at < text.size();)
  {
    DWORD written = 0;
    const auto count = static_cast<DWORD>(
        std::min<std::size_t>(text.size() - at, 1024 * 1024));
    if (!WriteFile(file, text.data() + at, count, &written, nullptr))
    {
      failure = GetLastError();
      break;
    }
    if (!written)
    {
      failure = ERROR_WRITE_FAULT;
      break;
    }
    at += written;
  }
  if (!failure && !FlushFileBuffers(file)) failure = GetLastError();
  if (!CloseHandle(file) && !failure) failure = GetLastError();
  if (failure) return std::unexpected(Error("Writing file", failure));
#else
  const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (file < 0) return std::unexpected(Error("Opening temporary file", errno));
  Cleanup cleanup{temporary};
  int failure = 0;
  for (std::size_t at = 0; at < text.size();)
  {
    const auto written =
        ::write(file, text.data() + at,
                std::min<std::size_t>(text.size() - at, 1024 * 1024));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0)
    {
      failure = written < 0 ? errno : EIO;
      break;
    }
    at += static_cast<std::size_t>(written);
  }
  if (!failure && ::fsync(file)) failure = errno;
  if (::close(file) && !failure) failure = errno;
  if (failure) return std::unexpected(Error("Writing file", failure));
#endif
  return Publish(temporary, path, overwrite);
}

std::expected<std::filesystem::path, std::string> ClaudePresetsPath()
{
  const auto settings = ClaudeSettingsPath();
  if (!settings) return std::unexpected(settings.error());
  return settings->parent_path().parent_path() / ".ptoolkit" /
         "claude_code_presets";
}

std::expected<std::filesystem::path, std::string> PresetStore::Path(
    std::string_view name) const
{
  if (name.empty() || name.size() > 160 || name.front() == ' ' ||
      name.back() == ' ' || name.back() == '.' ||
      std::any_of(name.begin(), name.end(),
                  [](unsigned char ch)
                  {
                    return ch < 32 || ch == 127 ||
                           std::string_view("<>:\"/\\|?*").find(ch) !=
                               std::string_view::npos;
                  }))
    return std::unexpected(
        "Use a nonempty name without path separators, reserved characters or "
        "trailing dots/spaces");
  std::string base(name.substr(0, name.find('.')));
  std::transform(base.begin(), base.end(), base.begin(), [](unsigned char ch)
                 { return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; });
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
      (base.size() == 4 &&
       (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '0' &&
       base[3] <= '9'))
    return std::unexpected("This name is reserved by the operating system");
  std::u8string filename(reinterpret_cast<const char8_t*>(name.data()),
                         name.size());
  filename += u8".json";
  return m_directory / std::filesystem::path(filename);
}

std::expected<std::vector<std::string>, std::string> PresetStore::List() const
{
  std::error_code ec;
  if (!std::filesystem::exists(m_directory, ec))
  {
    if (ec)
      return std::unexpected("Cannot access preset directory: " + ec.message());
    return std::vector<std::string>{};
  }
  std::vector<std::string> names;
  std::filesystem::directory_iterator it(m_directory, ec), end;
  if (ec) return std::unexpected("Cannot list presets: " + ec.message());
  for (; it != end; it.increment(ec))
  {
    if (ec) return std::unexpected("Cannot list presets: " + ec.message());
    if (!it->is_regular_file(ec))
    {
      if (ec) return std::unexpected("Cannot inspect preset: " + ec.message());
      continue;
    }
    if (it->path().extension() != ".json") continue;
    const auto name = it->path().stem().u8string();
    names.emplace_back(reinterpret_cast<const char*>(name.data()), name.size());
  }
  if (ec) return std::unexpected("Cannot list presets: " + ec.message());
  std::sort(names.begin(), names.end());
  return names;
}

std::expected<void, std::string> PresetStore::Create(
    std::string_view name, std::string_view text) const
{
  const auto path = Path(name);
  if (!path) return std::unexpected(path.error());
  std::error_code ec;
  std::filesystem::create_directories(m_directory, ec);
  if (ec)
    return std::unexpected("Cannot create preset directory: " + ec.message());
  return WriteConfigText(*path, text, false);
}
std::expected<std::string, std::string> PresetStore::Load(
    std::string_view name) const
{
  const auto path = Path(name);
  if (!path) return std::unexpected(path.error());
  return ReadConfigText(*path);
}
std::expected<void, std::string> PresetStore::Rename(
    std::string_view name, std::string_view replacement) const
{
  const auto from = Path(name), to = Path(replacement);
  if (!from) return std::unexpected(from.error());
  if (!to) return std::unexpected(to.error());
  if (name == replacement) return {};
  return Publish(*from, *to, false);
}
std::expected<void, std::string> PresetStore::Delete(
    std::string_view name) const
{
  const auto path = Path(name);
  if (!path) return std::unexpected(path.error());
  std::error_code ec;
  const auto removed = std::filesystem::remove(*path, ec);
  if (ec) return std::unexpected("Cannot delete preset: " + ec.message());
  if (!removed) return std::unexpected("The preset no longer exists");
  return {};
}
}  // namespace ptoolkit
