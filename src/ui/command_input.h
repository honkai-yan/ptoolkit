#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

namespace ptoolkit
{
struct Command
{
  std::string name;
  std::string description;
};

// A soft-wrapped command editor. The menu never takes keyboard focus away
// from the editor; only Up/Down and Tab are routed to visible suggestions.
class CommandInput : public ftxui::ComponentBase
{
public:
  CommandInput(std::vector<Command> commands,
               std::function<bool(const std::string&)> on_submit);
  bool Focusable() const override { return true; }
  bool OnEvent(ftxui::Event event) override;
  ftxui::Element OnRender() override;
  ftxui::Element RenderMenu() const;

  const std::string& Content() const { return m_content; }
  std::size_t Cursor() const { return m_cursor; }

private:
  class EditorNode;
  struct Row
  {
    std::size_t begin, end;
  };
  std::vector<Row> Rows() const;
  int CursorRow(const std::vector<Row>& rows) const;
  void MoveVertical(int direction);
  void Changed();

  std::vector<Command> m_commands;
  std::function<bool(const std::string&)> m_on_submit;
  std::string m_content;
  std::size_t m_cursor = 0;
  std::vector<std::size_t> m_matches;
  int m_selected = 0;
  int m_width = 1;
  int m_scroll = 0;
  ftxui::Box m_box;
  std::optional<int> m_preferred_column;
  bool m_follow_cursor = true;
};
}  // namespace ptoolkit
