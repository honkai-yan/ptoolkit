#include "ui/claude_config_view.h"

#include "app.h"
#include "ui/dialog.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <ptoolkit_editor/editor.h>
#include <ftxui/component/component.hpp>

namespace ptoolkit
{
class ClaudeConfigView::Sidebar : public ftxui::ComponentBase
{
public:
  enum class Action
  {
    Create,
    Load,
    Rename,
    Delete
  };
  explicit Sidebar(std::function<void(Action, const std::string&)> action)
      : m_action(std::move(action))
  {
  }
  bool Focusable() const override { return true; }
  void SetNames(std::vector<std::string> names, const std::string& selected)
  {
    m_names = std::move(names);
    const auto found = std::find(m_names.begin(), m_names.end(), selected);
    if (!selected.empty() && found != m_names.end())
      m_selected = static_cast<int>(found - m_names.begin()) + 1;
    m_selected = std::clamp(m_selected, 0, static_cast<int>(m_names.size()));
  }
  std::string SelectedName() const
  {
    return m_selected > 0 ? m_names[m_selected - 1] : std::string{};
  }
  bool Contains(ftxui::Event& event) const
  {
    return event.is_mouse() && m_box.Contain(event.mouse().x, event.mouse().y);
  }
  bool OnEvent(ftxui::Event event) override
  {
    using namespace ftxui;
    if (event.is_mouse())
    {
      if (!Contains(event)) return false;
      const auto& mouse = event.mouse();
      if (mouse.motion == Mouse::Pressed && mouse.button == Mouse::Left)
      {
        TakeFocus();
        if (m_header.Contain(mouse.x, mouse.y))
        {
          m_selected = 0;
          m_action(Action::Create, {});
        }
        else if (m_list_box.Contain(mouse.x, mouse.y))
          for (std::size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].Contain(mouse.x, mouse.y))
            {
              m_selected = static_cast<int>(i) + 1;
              break;
            }
      }
      else if (mouse.button == Mouse::WheelUp)
        m_selected = std::max(0, m_selected - 1);
      else if (mouse.button == Mouse::WheelDown)
        m_selected = std::min(static_cast<int>(m_names.size()), m_selected + 1);
      return true;
    }
    if (!Focused()) return false;
    if (event == Event::ArrowUp)
      m_selected = std::max(0, m_selected - 1);
    else if (event == Event::ArrowDown)
      m_selected = std::min(static_cast<int>(m_names.size()), m_selected + 1);
    else if (event == Event::Home)
      m_selected = 0;
    else if (event == Event::End)
      m_selected = static_cast<int>(m_names.size());
    else if (event == Event::Return)
      m_action(m_selected == 0 ? Action::Create : Action::Load, SelectedName());
    else if (m_selected > 0 && event == Event::Character('r'))
      m_action(Action::Rename, SelectedName());
    else if (m_selected > 0 && event == Event::Character('d'))
      m_action(Action::Delete, SelectedName());
    return true;
  }
  ftxui::Element OnRender() override
  {
    using namespace ftxui;
    const auto entry = [&](const std::string& name, bool selected)
    {
      auto row = text(" " + name) | xflex;
      if (selected)
        row |= bgcolor(Color::RGB(0x63, 0xa8, 0xf8)) | color(Color::Black);
      return row;
    };
    Elements rows;
    m_rows.resize(m_names.size());
    for (std::size_t i = 0; i < m_names.size(); ++i)
    {
      auto row = entry(m_names[i], m_selected == static_cast<int>(i) + 1) |
                 reflect(m_rows[i]);
      if (Focused() && m_selected == static_cast<int>(i) + 1) row |= focus;
      rows.push_back(std::move(row));
    }
    if (rows.empty()) rows.push_back(text(" No presets yet") | dim);
    return vbox({entry("New preset", m_selected == 0) | reflect(m_header),
                 separator(),
                 vbox(std::move(rows)) | vscroll_indicator | yframe | flex |
                     reflect(m_list_box),
                 separator(), text(" Enter: load  r: rename") | dim,
                 text(" d: delete  Ctrl+P: close") | dim}) |
           border | color(Color::RGB(0xe5, 0xe5, 0xe5)) |
           bgcolor(Color::RGB(0x25, 0x25, 0x26)) | clear_under | reflect(m_box);
  }

private:
  std::function<void(Action, const std::string&)> m_action;
  std::vector<std::string> m_names;
  int m_selected = 0;
  ftxui::Box m_box, m_header, m_list_box;
  std::vector<ftxui::Box> m_rows;
};

ClaudeConfigView::ClaudeConfigView(App& app, std::filesystem::path presets_path)
    : m_app(app)
{
  if (presets_path.empty())
  {
    auto resolved = ClaudePresetsPath();
    if (resolved)
      presets_path = std::move(*resolved);
    else
      m_store_error = resolved.error();
  }
  if (m_store_error.empty())
    m_store = std::make_unique<PresetStore>(std::move(presets_path));
  m_editor = std::make_shared<editor::Editor>();
  m_sidebar = std::make_shared<Sidebar>(
      [this](Sidebar::Action action, const std::string& name)
      {
        switch (action)
        {
          case Sidebar::Action::Create:
            CreatePreset();
            break;
          case Sidebar::Action::Load:
            LoadPreset(name);
            break;
          case Sidebar::Action::Rename:
            RenamePreset(name);
            break;
          case Sidebar::Action::Delete:
            DeletePreset(name);
            break;
        }
      });
  m_dialog = std::make_shared<Dialog>([this] { RestoreFocus(); });
  Add(m_editor);
  Add(m_sidebar);
  Add(m_dialog);
  m_active = m_editor;
}
void ClaudeConfigView::SyncDocument()
{
  if (auto document = m_app.GetClaudeDocument())
    m_editor->SetDocument(document);
}
void ClaudeConfigView::FocusEditor()
{
  SyncDocument();
  m_editor->TakeFocus();
}
bool ClaudeConfigView::HasDialog() const { return m_dialog->Visible(); }
ftxui::Component ClaudeConfigView::ActiveChild() { return m_active; }
void ClaudeConfigView::SetActiveChild(ftxui::ComponentBase* child)
{
  for (const auto& component :
       {ftxui::Component(m_editor), ftxui::Component(m_sidebar),
        ftxui::Component(m_dialog)})
    if (component.get() == child)
    {
      m_active = component;
      return;
    }
}
void ClaudeConfigView::RestoreFocus()
{
  if (m_return_to_sidebar && m_sidebar_open)
    m_sidebar->TakeFocus();
  else
    m_editor->TakeFocus();
}
void ClaudeConfigView::ShowError(std::string title, std::string message)
{
  m_return_to_sidebar = m_sidebar_open && m_sidebar->Focused();
  m_dialog->Message(std::move(title), std::move(message));
}
void ClaudeConfigView::RefreshPresets(const std::string& select)
{
  if (!m_store)
  {
    ShowError("Preset error", m_store_error);
    return;
  }
  auto names = m_store->List();
  if (!names)
  {
    ShowError("Preset error", names.error());
    return;
  }
  m_sidebar->SetNames(std::move(*names), select);
}
void ClaudeConfigView::CreatePreset()
{
  m_return_to_sidebar = true;
  m_dialog->Prompt("New preset", "Preset name (saved as <name>.json)", {},
                   [this](const std::string& name)
                   {
                     if (!m_store)
                     {
                       ShowError("Preset error", m_store_error);
                       return;
                     }
                     auto document = m_app.GetClaudeDocument();
                     if (!document)
                     {
                       ShowError("Preset error", "No configuration is open");
                       return;
                     }
                     const auto result =
                         m_store->Create(name, document->Text());
                     if (!result)
                       ShowError("Cannot save preset", result.error());
                     else
                       RefreshPresets(name);
                   });
}
void ClaudeConfigView::LoadPreset(const std::string& name)
{
  if (!m_store)
  {
    ShowError("Preset error", m_store_error);
    return;
  }
  auto contents = m_store->Load(name);
  if (!contents)
  {
    ShowError("Cannot load preset", contents.error());
    return;
  }
  if (auto document = m_app.GetClaudeDocument())
    document->ReplaceText(*contents);
}
void ClaudeConfigView::RenamePreset(const std::string& name)
{
  m_return_to_sidebar = true;
  m_dialog->Prompt("Rename preset", "New preset name", name,
                   [this, name](const std::string& replacement)
                   {
                     const auto result = m_store->Rename(name, replacement);
                     if (!result)
                       ShowError("Cannot rename preset", result.error());
                     else
                       RefreshPresets(replacement);
                   });
}
void ClaudeConfigView::DeletePreset(const std::string& name)
{
  m_return_to_sidebar = true;
  m_dialog->ConfirmAction("Delete preset", "Delete preset \"" + name + "\"?",
                          [this, name](const std::string&)
                          {
                            const auto result = m_store->Delete(name);
                            if (!result)
                              ShowError("Cannot delete preset", result.error());
                            else
                              RefreshPresets();
                          });
}
bool ClaudeConfigView::OnEvent(ftxui::Event event)
{
  using namespace ftxui;
  SyncDocument();
  if (m_dialog->Visible()) return m_dialog->OnEvent(event);
  if (event == Event::CtrlS)
  {
    const auto result = m_app.SaveClaudeSettings();
    if (!result) ShowError("Save failed", result.error());
    return true;
  }
  if (event == Event::CtrlP)
  {
    m_sidebar_open = !m_sidebar_open;
    if (m_sidebar_open)
    {
      m_sidebar->TakeFocus();
      RefreshPresets(m_sidebar->SelectedName());
    }
    else
      m_editor->TakeFocus();
    return true;
  }
  if (event.is_mouse())
  {
    if (m_sidebar_open && m_sidebar->Contains(event))
      return m_sidebar->OnEvent(event);
    return m_editor->OnEvent(event);
  }
  if (m_sidebar_open && m_sidebar->Focused()) return m_sidebar->OnEvent(event);
  return m_editor->OnEvent(event);
}
ftxui::Element ClaudeConfigView::OnRender()
{
  using namespace ftxui;
  SyncDocument();
  Element page;
  if (auto document = m_app.GetClaudeDocument())
  {
    auto area = m_editor->Render() | flex;
    if (m_sidebar_open)
      area = dbox({area, hbox({m_sidebar->Render() | size(WIDTH, EQUAL, 32),
                               filler()})});
    page = vbox({text(std::string("~/.claude/settings.json") +
                      (document->Modified() ? " *" : "")),
                 area | flex,
                 text(m_editor->ClipboardError().empty()
                          ? "Ctrl+S: save | Ctrl+P: presets | F6: editor / "
                            "command | Ctrl+D: next match"
                          : m_editor->ClipboardError()) |
                     dim});
  }
  else
    page = vbox({text("~/.claude/settings.json"),
                 paragraph(m_app.GetClaudeError()) | color(Color::RedLight),
                 filler()});
  if (m_dialog->Visible())
    return dbox({page | flex, m_dialog->Render()}) | flex;
  return page | flex;
}
}  // namespace ptoolkit
