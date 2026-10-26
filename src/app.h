#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ptoolkit
{
namespace editor
{
class Document;
}
struct HardwareInfo
{
  std::string cpu;
  std::string gpu;
  std::string memory;
  std::string disk;
};

class App
{
public:
  enum class Page
  {
    Home,
    Hello,
    ClaudeCodeConfig
  };

  static App& GetInstance();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Collect all information before publishing the new application state.
  std::expected<void, std::string> Init();
  const HardwareInfo& GetHardwareInfo() const { return m_hardware_info; }
  const std::string& GetSystemEdition() const { return m_system_edition; }
  Page GetPage() const { return m_page; }
  bool ExecuteCommand(std::string_view command,
                      const std::filesystem::path& claude_settings_path = {});
  std::shared_ptr<editor::Document> GetClaudeDocument() const
  {
    return m_claude_document;
  }
  const std::string& GetClaudeError() const { return m_claude_error; }
  std::expected<void, std::string> SaveClaudeSettings();

private:
  App() = default;

  HardwareInfo m_hardware_info;
  std::string m_system_edition;
  Page m_page = Page::Home;
  std::shared_ptr<editor::Document> m_claude_document;
  std::string m_claude_error;
  std::filesystem::path m_claude_path;
};
}  // namespace ptoolkit
