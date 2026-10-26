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
    ClaudeCodeConfig,
    ClaudeCodePrompt
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
                      const std::filesystem::path& claude_settings_path = {},
                      const std::filesystem::path& claude_prompt_path = {});
  std::shared_ptr<editor::Document> GetClaudeDocument() const
  {
    return m_claude.document;
  }
  const std::string& GetClaudeError() const { return m_claude.error; }
  std::expected<void, std::string> SaveClaudeSettings();
  std::shared_ptr<editor::Document> GetClaudePromptDocument() const
  {
    return m_prompt.document;
  }
  const std::string& GetClaudePromptError() const { return m_prompt.error; }
  std::expected<void, std::string> SaveClaudePrompt();
  // Document of the currently open file page, or null on other pages.
  std::shared_ptr<editor::Document> GetActiveDocument() const;

private:
  App() = default;
  struct FileState
  {
    std::shared_ptr<editor::Document> document;
    std::string error;
    std::filesystem::path path;
  };
  static FileState OpenConfig(const std::filesystem::path& requested);
  static FileState OpenPrompt(const std::filesystem::path& requested);
  static std::expected<void, std::string> Save(FileState& state,
                                               bool create_parent);

  HardwareInfo m_hardware_info;
  std::string m_system_edition;
  Page m_page = Page::Home;
  FileState m_claude;
  FileState m_prompt;
};
}  // namespace ptoolkit
