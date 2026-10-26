#pragma once

#include "config_storage.h"

#include <memory>
#include <string>

#include <ftxui/component/component_base.hpp>

namespace ptoolkit
{
class App;
class Dialog;
namespace editor
{
class Editor;
}

class ClaudeConfigView : public ftxui::ComponentBase
{
public:
  explicit ClaudeConfigView(App& app, std::filesystem::path presets_path = {});
  void FocusEditor();
  bool HasDialog() const;
  bool Focusable() const override { return true; }
  ftxui::Component ActiveChild() override;
  void SetActiveChild(ftxui::ComponentBase* child) override;
  bool OnEvent(ftxui::Event event) override;
  ftxui::Element OnRender() override;

private:
  class Sidebar;
  void SyncDocument();
  void RefreshPresets(const std::string& select = {});
  void ShowError(std::string title, std::string message);
  void CreatePreset();
  void LoadPreset(const std::string& name);
  void RenamePreset(const std::string& name);
  void DeletePreset(const std::string& name);
  void RestoreFocus();

  App& m_app;
  std::unique_ptr<PresetStore> m_store;
  std::string m_store_error;
  std::shared_ptr<editor::Editor> m_editor;
  std::shared_ptr<Sidebar> m_sidebar;
  std::shared_ptr<Dialog> m_dialog;
  ftxui::Component m_active;
  bool m_sidebar_open = false;
  bool m_return_to_sidebar = false;
};
}  // namespace ptoolkit
