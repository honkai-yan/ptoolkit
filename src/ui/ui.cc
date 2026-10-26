#include "ui/ui.h"

#include "app.h"
#include "ui/command_input.h"
#include "ui/preset_editor_view.h"
#include "config.h"

#include <algorithm>
#include <expected>
#include <format>
#include <memory>
#include <utility>

#include <ptoolkit_editor/document.h>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/util/autoreset.hpp>

namespace ptoolkit
{
namespace
{
struct PageScroll
{
  ftxui::Box box;
  int offset = 0;
  int maximum = 0;
};

// Only this viewport scrolls; the title, composer and popup are its siblings.
class PageViewport : public ftxui::Node
{
public:
  PageViewport(ftxui::Element page, std::shared_ptr<PageScroll> scroll)
      : Node({std::move(page)}), m_scroll(std::move(scroll))
  {
  }

  void ComputeRequirement() override
  {
    Node::ComputeRequirement();
    requirement_ = children_[0]->requirement();
    requirement_.min_y = 0;
    requirement_.flex_grow_y = 1;
    requirement_.flex_shrink_y = 1;
  }

  void SetBox(ftxui::Box box) override
  {
    Node::SetBox(box);
    m_scroll->box = box;
    const int height = children_[0]->requirement().min_y;
    m_scroll->maximum = std::max(0, height - (box.y_max - box.y_min + 1));
    m_scroll->offset = std::clamp(m_scroll->offset, 0, m_scroll->maximum);
    box.y_min -= m_scroll->offset;
    box.y_max = box.y_min + height - 1;
    children_[0]->SetBox(box);
  }

  void Render(ftxui::Screen& screen) override
  {
    const ftxui::AutoReset<ftxui::Box> stencil(
        &screen.stencil, ftxui::Box::Intersection(box_, screen.stencil));
    Node::Render(screen);
  }

private:
  std::shared_ptr<PageScroll> m_scroll;
};

bool IsEditorPage(App::Page page)
{
  return page == App::Page::ClaudeCodeConfig ||
         page == App::Page::ClaudeCodePrompt;
}

template <typename Fallback>
std::expected<std::filesystem::path, std::string> PresetsPath(
    const std::filesystem::path& given, Fallback fallback)
{
  if (!given.empty()) return given;
  return fallback();
}
}  // namespace

ftxui::Component CreateApplicationView(
    App& app, std::function<void()> on_exit,
    std::filesystem::path claude_settings_path,
    std::filesystem::path claude_presets_path,
    std::filesystem::path claude_prompt_path,
    std::filesystem::path claude_prompt_presets_path)
{
  using namespace ftxui;
  auto scroll = std::make_shared<PageScroll>();
  const auto config_presets = PresetsPath(claude_presets_path,
                                          ClaudePresetsPath);
  const auto prompt_presets = PresetsPath(claude_prompt_presets_path,
                                          ClaudePromptPresetsPath);
  PresetEditorOptions config_options{
      .presets_path = config_presets ? *config_presets
                                     : std::filesystem::path{},
      .presets_error = config_presets ? std::string{}
                                      : config_presets.error(),
      .file_label = "~/.claude/settings.json",
      .preset_extension = ".json",
      .get_document = [&app] { return app.GetClaudeDocument(); },
      .get_error = [&app]() -> const std::string&
      { return app.GetClaudeError(); },
      .save = [&app] { return app.SaveClaudeSettings(); },
      .json_highlight = true,
  };
  PresetEditorOptions prompt_options{
      .presets_path = prompt_presets ? *prompt_presets
                                     : std::filesystem::path{},
      .presets_error = prompt_presets ? std::string{}
                                      : prompt_presets.error(),
      .file_label = "~/.claude/CLAUDE.md",
      .preset_extension = ".md",
      .get_document = [&app] { return app.GetClaudePromptDocument(); },
      .get_error = [&app]() -> const std::string&
      { return app.GetClaudePromptError(); },
      .save = [&app] { return app.SaveClaudePrompt(); },
      .json_highlight = false,
  };
  auto config_view =
      std::make_shared<PresetEditorView>(std::move(config_options));
  auto prompt_view =
      std::make_shared<PresetEditorView>(std::move(prompt_options));
  auto input = std::make_shared<CommandInput>(
      std::vector<Command>{
          {"home", "System and hardware information"},
          {"hello", "Show hello world"},
          {"claude_code_config_man", "Edit Claude Code user settings"},
          {"claude_code_prompt_man", "Edit global Claude Code prompt"}},
      [&app, scroll, config_view, prompt_view, claude_settings_path,
       claude_prompt_path](const std::string& command)
      {
        if (!app.ExecuteCommand(command, claude_settings_path,
                                claude_prompt_path))
          return false;
        scroll->offset = 0;
        if (app.GetPage() == App::Page::ClaudeCodeConfig &&
            app.GetClaudeDocument())
          config_view->FocusEditor();
        else if (app.GetPage() == App::Page::ClaudeCodePrompt &&
                 app.GetClaudePromptDocument())
          prompt_view->FocusEditor();
        return true;
      });
  auto components = Container::Vertical(
      {Maybe(config_view,
             [&app]
             {
               return app.GetPage() == App::Page::ClaudeCodeConfig &&
                      app.GetClaudeDocument();
             }),
       Maybe(prompt_view,
             [&app]
             {
               return app.GetPage() == App::Page::ClaudeCodePrompt &&
                      app.GetClaudePromptDocument();
             }),
       input});
  auto view = Renderer(
      components,
      [&app, input, scroll, config_view, prompt_view]
      {
        Element page;
        if (app.GetPage() == App::Page::Hello)
          page = text("hello world");
        else if (app.GetPage() == App::Page::ClaudeCodeConfig)
          page = config_view->Render();
        else if (app.GetPage() == App::Page::ClaudeCodePrompt)
          page = prompt_view->Render();
        else
        {
          const auto& hardware = app.GetHardwareInfo();
          auto table = Table({
              {"System", app.GetSystemEdition()},
              {"CPU", hardware.cpu},
              {"Memory", hardware.memory},
              {"GPU", hardware.gpu},
              {"Hard disk", hardware.disk},
          });
          table.SelectAll().Border(LIGHT);
          table.SelectAll().Separator(LIGHT);
          table.SelectColumn(0).DecorateCells(align_right);
          page = table.Render();
        }
        Element main_content =
            IsEditorPage(app.GetPage())
                ? page | flex
                : std::make_shared<PageViewport>(page, scroll);
        auto content =
            dbox({main_content,
                  vbox({filler(), input->Focused() ? input->RenderMenu()
                                                   : emptyElement()})}) |
            flex;
        return vbox({text(std::format("{} {}", kAppName, GetAppVersion())) |
                         hcenter,
                     separator(), content, input->Render()}) |
               borderRounded | flex;
      });
  input->TakeFocus();
  return CatchEvent(
      view,
      [on_exit = std::move(on_exit), scroll, &app, input, config_view,
       prompt_view](Event event)
      {
        PresetEditorView* editor_view = nullptr;
        if (app.GetPage() == App::Page::ClaudeCodeConfig)
          editor_view = config_view.get();
        else if (app.GetPage() == App::Page::ClaudeCodePrompt)
          editor_view = prompt_view.get();
        if (editor_view)
        {
          if (editor_view->HasDialog()) return editor_view->OnEvent(event);
          if (event == Event::CtrlS || event == Event::CtrlP)
            return editor_view->OnEvent(event);
        }
        if (event == Event::CtrlC)
        {
          if (editor_view && app.GetActiveDocument() &&
              editor_view->Focused())
            return editor_view->OnEvent(event);
          on_exit();
          return true;
        }
        if (event == Event::F6 && editor_view && app.GetActiveDocument())
        {
          app.GetActiveDocument()->BreakUndoGroup();
          if (editor_view->Focused())
            input->TakeFocus();
          else
            editor_view->FocusEditor();
          return true;
        }
        // Keep Ctrl+Z from suspending the process outside the editor.
        if (event == Event::CtrlZ &&
            !(editor_view && editor_view->Focused()))
          return true;
        if (event.is_mouse() && !IsEditorPage(app.GetPage()) &&
            scroll->box.Contain(event.mouse().x, event.mouse().y))
        {
          if (event.mouse().button == Mouse::WheelUp)
            scroll->offset = std::max(0, scroll->offset - 1);
          else if (event.mouse().button == Mouse::WheelDown)
            scroll->offset = std::min(scroll->maximum, scroll->offset + 1);
          else
            return false;
          return true;
        }
        return false;
      });
}
}  // namespace ptoolkit
