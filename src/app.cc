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

App::FileState App::OpenConfig(const std::filesystem::path& requested)
{
  FileState state;
  auto path = requested.empty() ? ClaudeSettingsPath()
                                : std::expected<std::filesystem::path,
                                                std::string>(requested);
  if (!path)
  {
    state.error = path.error();
    return state;
  }
  auto config = LoadClaudeConfig(*path);
  if (!config)
  {
    state.error = config.error();
    state.path = *path;
    return state;
  }
  state.path = config->path;
  state.document = std::move(config->document);
  return state;
}

App::FileState App::OpenPrompt(const std::filesystem::path& requested)
{
  FileState state;
  auto path = requested.empty() ? ClaudePromptPath()
                                : std::expected<std::filesystem::path,
                                                std::string>(requested);
  if (!path)
  {
    state.error = path.error();
    return state;
  }
  auto prompt = LoadClaudePrompt(*path);
  if (!prompt)
  {
    state.error = prompt.error();
    state.path = *path;
    return state;
  }
  state.path = prompt->path;
  state.document = std::move(prompt->document);
  return state;
}

std::expected<void, std::string> App::Save(FileState& state,
                                           bool create_parent)
{
  if (!state.document || state.path.empty())
    return std::unexpected("No Claude Code file is open");
  if (create_parent)
  {
    const auto parent = state.path.parent_path();
    if (!parent.empty())
    {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec)
        return std::unexpected("Cannot create " + parent.string() + ": " +
                               ec.message());
    }
  }
  auto result = WriteConfigText(state.path, state.document->Text());
  if (result) state.document->MarkSaved();
  return result;
}

bool App::ExecuteCommand(std::string_view command,
                         const std::filesystem::path& claude_settings_path,
                         const std::filesystem::path& claude_prompt_path)
{
  if (command == "home")
    m_page = Page::Home;
  else if (command == "hello")
    m_page = Page::Hello;
  else if (command == "claude_code_config_man")
  {
    m_page = Page::ClaudeCodeConfig;
    m_claude = OpenConfig(claude_settings_path);
  }
  else if (command == "claude_code_prompt_man")
  {
    m_page = Page::ClaudeCodePrompt;
    m_prompt = OpenPrompt(claude_prompt_path);
  }
  else
    return false;
  return true;
}

std::expected<void, std::string> App::SaveClaudeSettings()
{
  return Save(m_claude, false);
}

std::expected<void, std::string> App::SaveClaudePrompt()
{
  return Save(m_prompt, true);
}

std::shared_ptr<editor::Document> App::GetActiveDocument() const
{
  if (m_page == Page::ClaudeCodeConfig) return m_claude.document;
  if (m_page == Page::ClaudeCodePrompt) return m_prompt.document;
  return {};
}

std::expected<void, std::string> App::Init()
{
#if defined(_WIN32) || defined(__APPLE__)
  const auto en = system_info::GetNameEn();
  const auto display = system_info::GetDisplayVersion();
  const auto build = system_info::GetBuildVersion();
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
#ifdef __APPLE__
    if (gpu.dedicated_memory_bytes == 0)
    {
      hardware.gpu += gpu.brand_name + " (shared/system memory)";
      continue;
    }
#endif
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
