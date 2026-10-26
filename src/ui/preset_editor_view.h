#pragma once

#include "config_storage.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component_base.hpp>

namespace ptoolkit
{
class Dialog;
namespace editor
{
class Document;
class Editor;
}

// A file editor page with JSON/plain-text highlighting and a preset sidebar.
// All state access goes through the callbacks so the same view serves Claude
// Code settings and the global prompt.
struct PresetEditorOptions
{
  std::filesystem::path presets_path;
  std::string presets_error;
  std::string file_label;
  std::string preset_extension = ".json";
  std::function<std::shared_ptr<editor::Document>()> get_document;
  std::function<const std::string&()> get_error;
  std::function<std::expected<void, std::string>()> save;
  bool json_highlight = true;
};

class PresetEditorView : public ftxui::ComponentBase
{
public:
  explicit PresetEditorView(PresetEditorOptions options);
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

  PresetEditorOptions m_options;
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
