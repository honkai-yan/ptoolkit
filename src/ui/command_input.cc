#include "ui/command_input.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

namespace ptoolkit
{
namespace
{
struct Glyph
{
  std::string text;
  std::size_t begin, end;
  int width;
};

std::vector<Glyph> Glyphs(const std::string& text)
{
  std::vector<Glyph> result;
  std::size_t offset = 0;
  for (auto& glyph : ftxui::Utf8ToGlyphs(text))
  {
    // FTXUI inserts empty entries for the second cell of a wide glyph.
    if (glyph.empty()) continue;
    const auto end = offset + glyph.size();
    const int width = ftxui::string_width(glyph);
    result.push_back({std::move(glyph), offset, end, width});
    offset = end;
  }
  return result;
}

std::size_t Previous(const std::string& text, std::size_t cursor)
{
  std::size_t previous = 0;
  for (const auto& glyph : Glyphs(text))
  {
    if (glyph.end >= cursor) return glyph.begin;
    previous = glyph.begin;
  }
  return previous;
}

std::size_t Next(const std::string& text, std::size_t cursor)
{
  for (const auto& glyph : Glyphs(text))
    if (glyph.end > cursor) return glyph.end;
  return text.size();
}
}  // namespace

CommandInput::CommandInput(std::vector<Command> commands,
                           std::function<bool(const std::string&)> on_submit)
    : m_commands(std::move(commands)), m_on_submit(std::move(on_submit))
{
}

std::vector<CommandInput::Row> CommandInput::Rows() const
{
  std::vector<Row> rows;
  std::size_t begin = 0;
  int column = 0;
  for (const auto& glyph : Glyphs(m_content))
  {
    if (column > 0 && column + glyph.width > m_width)
    {
      rows.push_back({begin, glyph.begin});
      begin = glyph.begin;
      column = 0;
    }
    column += glyph.width;
    if (column >= m_width)
    {
      rows.push_back({begin, glyph.end});
      begin = glyph.end;
      column = 0;
    }
  }
  // Always keep a cell available for the caret, including at a wrap boundary.
  rows.push_back({begin, m_content.size()});
  return rows;
}

int CommandInput::CursorRow(const std::vector<Row>& rows) const
{
  int row = 0;
  while (row + 1 < static_cast<int>(rows.size()) &&
         rows[row + 1].begin <= m_cursor)
    ++row;
  return row;
}

void CommandInput::MoveVertical(int direction)
{
  const auto rows = Rows();
  const int current = CursorRow(rows);
  if (!m_preferred_column)
    m_preferred_column = ftxui::string_width(
        m_content.substr(rows[current].begin, m_cursor - rows[current].begin));
  const int target =
      std::clamp(current + direction, 0, static_cast<int>(rows.size()) - 1);
  if (target == current) return;
  m_cursor = rows[target].begin;
  int column = 0;
  for (const auto& glyph : Glyphs(m_content.substr(
           rows[target].begin, rows[target].end - rows[target].begin)))
  {
    if (column + glyph.width > *m_preferred_column) break;
    // An end shared with the next row belongs to that next row.
    if (target + 1 < static_cast<int>(rows.size()) &&
        rows[target].begin + glyph.end == rows[target + 1].begin)
      break;
    column += glyph.width;
    m_cursor = rows[target].begin + glyph.end;
  }
}

void CommandInput::Changed()
{
  m_matches.clear();
  if (!m_content.empty())
    for (std::size_t i = 0; i < m_commands.size(); ++i)
      if (m_commands[i].name.starts_with(m_content)) m_matches.push_back(i);
  m_selected = 0;
  m_preferred_column.reset();
  m_follow_cursor = true;
}

bool CommandInput::OnEvent(ftxui::Event event)
{
  using ftxui::Event;
  if (event.is_mouse())
  {
    if (!m_box.Contain(event.mouse().x, event.mouse().y)) return false;
    if (event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed)
    {
      TakeFocus();
      return true;
    }
    if (event.mouse().button == ftxui::Mouse::WheelUp ||
        event.mouse().button == ftxui::Mouse::WheelDown)
    {
      m_scroll += event.mouse().button == ftxui::Mouse::WheelUp ? -1 : 1;
      m_follow_cursor = false;
      return true;
    }
    return false;
  }
  if (event == Event::Return)
  {
    if (!m_content.empty() && m_on_submit(m_content))
    {
      m_content.clear();
      m_cursor = 0;
      m_scroll = 0;
      Changed();
    }
    return true;
  }
  if (!m_matches.empty())
  {
    const int count = static_cast<int>(m_matches.size());
    if (event == Event::ArrowUp || event == Event::ArrowDown)
    {
      m_selected =
          (m_selected + (event == Event::ArrowUp ? -1 : 1) + count) % count;
      return true;
    }
    if (event == Event::Tab)
    {
      m_content = m_commands[m_matches[m_selected]].name;
      m_cursor = m_content.size();
      m_matches.clear();
      m_preferred_column.reset();
      m_follow_cursor = true;
      return true;
    }
  }

  m_follow_cursor = true;
  if (event == Event::ArrowUp || event == Event::ArrowDown)
  {
    MoveVertical(event == Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == Event::ArrowLeft || event == Event::ArrowRight)
  {
    m_cursor = event == Event::ArrowLeft ? Previous(m_content, m_cursor)
                                         : Next(m_content, m_cursor);
    m_preferred_column.reset();
    return true;
  }
  if (event == Event::Home || event == Event::End || event == Event::CtrlA ||
      event == Event::CtrlE)
  {
    const auto rows = Rows();
    const int row = CursorRow(rows);
    if (event == Event::CtrlA)
      m_cursor = 0;
    else if (event == Event::CtrlE)
      m_cursor = m_content.size();
    else if (event == Event::Home)
      m_cursor = rows[row].begin;
    else
      m_cursor = row + 1 < static_cast<int>(rows.size())
                     ? Previous(m_content, rows[row].end)
                     : rows[row].end;
    m_preferred_column.reset();
    return true;
  }
  if (event == Event::Backspace)
  {
    const auto begin = Previous(m_content, m_cursor);
    m_content.erase(begin, m_cursor - begin);
    m_cursor = begin;
    Changed();
    return true;
  }
  if (event == Event::Delete)
  {
    m_content.erase(m_cursor, Next(m_content, m_cursor) - m_cursor);
    Changed();
    return true;
  }
  if (event.is_character())
  {
    auto value = event.character();
    // Pasted line breaks become spaces; Enter always submits, never inserts.
    for (char& ch : value)
      if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    std::erase_if(value, [](unsigned char ch) { return ch < 32 || ch == 127; });
    if (value.empty()) return false;
    m_content.insert(m_cursor, value);
    m_cursor += value.size();
    Changed();
    return true;
  }
  return false;
}

// Measure against the actual assigned width, not the previous terminal size.
// A second layout pass lets the parent anchor a resized editor at the bottom.
class CommandInput::EditorNode : public ftxui::Node
{
public:
  explicit EditorNode(CommandInput& input) : m_input(input) {}

  void ComputeRequirement() override
  {
    requirement_ = {};
    requirement_.min_y =
        std::min(8, static_cast<int>(m_input.Rows().size())) + 2;
    requirement_.flex_grow_x = 1;
    requirement_.flex_shrink_y = 1;
    if (m_input.Focused())
    {
      requirement_.focused.enabled = true;
      requirement_.focused.node = this;
      requirement_.focused.cursor_shape = ftxui::Screen::Cursor::BarBlinking;
    }
  }

  void SetBox(ftxui::Box box) override
  {
    Node::SetBox(box);
    m_input.m_box = box;
    const int width = std::max(1, box.x_max - box.x_min - 1);
    m_changed = width != m_input.m_width;
    m_input.m_width = width;
  }

  void Check(Status* status) override
  {
    status->need_iteration |= status->iteration == 0 || m_changed;
    m_changed = false;
  }

  void Render(ftxui::Screen& screen) override
  {
    // Terminal default backgrounds cannot receive an alpha overlay.
    const auto background = ftxui::Color::RGB(0x29, 0x29, 0x29);
    const auto foreground = ftxui::Color::RGB(0xf4, 0xf4, 0xf4);
    const auto bounds = ftxui::Box::Intersection(box_, screen.stencil);
    for (int y = bounds.y_min; y <= bounds.y_max; ++y)
      for (int x = bounds.x_min; x <= bounds.x_max; ++x)
      {
        auto& cell = screen.CellAt(x, y);
        cell.character = " ";
        cell.background_color = background;
        cell.foreground_color = foreground;
      }

    const auto rows = m_input.Rows();
    const int height = std::max(1, box_.y_max - box_.y_min - 1);
    const int cursor_row = m_input.CursorRow(rows);
    if (m_input.m_follow_cursor)
      m_input.m_scroll =
          std::clamp(m_input.m_scroll, cursor_row - height + 1, cursor_row);
    m_input.m_scroll =
        std::clamp(m_input.m_scroll, 0,
                   std::max(0, static_cast<int>(rows.size()) - height));
    for (int line = 0; line < height; ++line)
    {
      const int row = line + m_input.m_scroll;
      if (row >= static_cast<int>(rows.size())) break;
      int x = box_.x_min + 1;
      const int y = box_.y_min + 1 + line;
      for (const auto& glyph : Glyphs(m_input.m_content.substr(
               rows[row].begin, rows[row].end - rows[row].begin)))
      {
        if (y >= bounds.y_min && y <= bounds.y_max && x >= bounds.x_min &&
            x + glyph.width - 1 <= bounds.x_max - 1)
        {
          screen.CellAt(x, y).character = glyph.text;
          if (glyph.width == 2) screen.CellAt(x + 1, y).character = "";
        }
        x += glyph.width;
      }
    }
    const int column = ftxui::string_width(m_input.m_content.substr(
        rows[cursor_row].begin, m_input.m_cursor - rows[cursor_row].begin));
    const int y = box_.y_min + 1 + cursor_row - m_input.m_scroll;
    if (!m_input.Focused()) return;
    if (y > box_.y_min && y < box_.y_max && y >= bounds.y_min &&
        y <= bounds.y_max)
      screen.SetCursor(
          {box_.x_min + 1 + column, y, ftxui::Screen::Cursor::BarBlinking});
    else
      screen.SetCursor({0, 0, ftxui::Screen::Cursor::Hidden});
  }

private:
  CommandInput& m_input;
  bool m_changed = false;
};

ftxui::Element CommandInput::OnRender()
{
  return std::make_shared<EditorNode>(*this);
}

ftxui::Element CommandInput::RenderMenu() const
{
  using namespace ftxui;
  Elements entries;
  for (int i = 0; i < static_cast<int>(m_matches.size()); ++i)
  {
    const auto& command = m_commands[m_matches[i]];
    auto entry = text(" " + command.name + "  " + command.description);
    if (i == m_selected)
      entry |= bgcolor(Color::RGB(0x63, 0xa8, 0xf8)) | color(Color::Black);
    entries.push_back(std::move(entry));
  }
  if (entries.empty()) return emptyElement();
  return vbox(std::move(entries)) | clear_under;
}
}  // namespace ptoolkit
