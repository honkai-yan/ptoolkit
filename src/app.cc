#include "app.h"

#include "claude_config.h"
#include "config_storage.h"
#include "hardware_info.h"
#include "system_info.h"

#include <format>
#include <utility>

namespace ptoolkit
{
App& App::GetInstance()
{
  static App app;
  return app;
}

bool App::ExecuteCommand(std::string_view command,
                         const std::filesystem::path& claude_settings_path)
{
  if (command == "home")
    m_page = Page::Home;
  else if (command == "hello")
    m_page = Page::Hello;
  else if (command == "claude_code_config_man")
  {
    m_page = Page::ClaudeCodeConfig;
    m_claude_document.reset();
    m_claude_error.clear();
    m_claude_path.clear();
    auto path = claude_settings_path.empty()
                    ? ClaudeSettingsPath()
                    : std::expected<std::filesystem::path, std::string>(
                          claude_settings_path);
    if (!path)
      m_claude_error = path.error();
    else
    {
      auto config = LoadClaudeConfig(*path);
      if (!config)
        m_claude_error = config.error();
      else
      {
        m_claude_path = config->path;
        m_claude_document = std::move(config->document);
      }
    }
  }
  else
    return false;
  return true;
}

std::expected<void, std::string> App::SaveClaudeSettings()
{
  if (!m_claude_document || m_claude_path.empty())
    return std::unexpected("No Claude Code configuration is open");
  auto result = WriteConfigText(m_claude_path, m_claude_document->Text());
  if (result) m_claude_document->MarkSaved();
  return result;
}

std::expected<void, std::string> App::Init()
{
#ifdef _WIN32
  using namespace system_info::windows_version;
  const auto en = GetWindowsNameEn();
  const auto display = GetWindowsDisplayVersion();
  const auto build = GetWindowsBuildVersion();
  for (const auto* result : {&en, &display, &build})
  {
    if (!result->has_value()) return std::unexpected(result->error());
  }
  auto system_edition = std::format("{},{},{}", *en, *display, *build);

  const auto brand = hardware_info::GetCpuBrandName();
  if (!brand) return std::unexpected(brand.error());
  const auto cores = hardware_info::GetCpuCoreCount();
  if (!cores) return std::unexpected(cores.error());
  HardwareInfo hardware;
  hardware.cpu = std::format("{} with {} cores", *brand, *cores);

  constexpr double bytes_per_gib = 1024.0 * 1024.0 * 1024.0;
  const auto gpus = hardware_info::GetGpus();
  if (!gpus) return std::unexpected(gpus.error());
  if (gpus->empty()) hardware.gpu = "No hardware adapters found";
  for (const auto& gpu : *gpus)
  {
    if (!hardware.gpu.empty()) hardware.gpu += "; ";
    hardware.gpu += std::format("{} {:.2f} GiB", gpu.brand_name,
                                gpu.dedicated_memory_bytes / bytes_per_gib);
  }

  const auto memory = hardware_info::GetTotalMemoryBytes();
  if (!memory) return std::unexpected(memory.error());
  hardware.memory = std::format("{:.2f} GiB", *memory / bytes_per_gib);

  const auto disk = hardware_info::GetTotalDiskBytes();
  if (!disk) return std::unexpected(disk.error());
  hardware.disk = std::format("{:.2f} GiB", *disk / bytes_per_gib);

  m_system_edition = std::move(system_edition);
  m_hardware_info = std::move(hardware);
#endif
  return {};
}
}  // namespace ptoolkit
