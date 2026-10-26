#include "ptoolkit_editor/editor.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace ptoolkit::editor
{
namespace
{
struct KeyMovement
{
  std::string_view key;
  Movement movement;
  bool select;
};
constexpr KeyMovement movement_keys[] = {
    {"\x1b[D", Movement::Left, false},
    {"\x1b[C", Movement::Right, false},
    {"\x1b[A", Movement::Up, false},
    {"\x1b[B", Movement::Down, false},
    {"\x1b[H", Movement::Home, false},
    {"\x1b[F", Movement::End, false},
    {"\x1b[1;5D", Movement::WordLeft, false},
    {"\x1b[1;5C", Movement::WordRight, false},
    {"\x1b[1;5H", Movement::DocumentHome, false},
    {"\x1b[1;5F", Movement::DocumentEnd, false},
    {"\x1b[7;5~", Movement::DocumentHome, false},
    {"\x1b[8;5~", Movement::DocumentEnd, false},
    {"\x1b[1;5~", Movement::DocumentHome, false},
    {"\x1b[4;5~", Movement::DocumentEnd, false},
    {"\x1b[1;2D", Movement::Left, true},
    {"\x1b[1;2C", Movement::Right, true},
    {"\x1b[1;2A", Movement::Up, true},
    {"\x1b[1;2B", Movement::Down, true},
    {"\x1b[1;2H", Movement::Home, true},
    {"\x1b[1;2F", Movement::End, true},
    {"\x1b[1;6D", Movement::WordLeft, true},
    {"\x1b[1;6C", Movement::WordRight, true},
    {"\x1b[1;6H", Movement::DocumentHome, true},
    {"\x1b[1;6F", Movement::DocumentEnd, true},
    {"\x1b[7;6~", Movement::DocumentHome, true},
    {"\x1b[8;6~", Movement::DocumentEnd, true},
    {"\x1b[1;6~", Movement::DocumentHome, true},
    {"\x1b[4;6~", Movement::DocumentEnd, true},
#if defined(__APPLE__)
    // macOS terminals send Option as Alt/Meta. xterm-style terminals use
    // CSI 1;3 (Option) and CSI 1;4 (Option+Shift); iTerm2's natural text
    // editing and Terminal.app's "Option as Meta" send ESC b/f and ESC B/F.
    {"\x1b[1;3D", Movement::WordLeft, false},
    {"\x1b[1;3C", Movement::WordRight, false},
    {"\x1b[1;4D", Movement::WordLeft, true},
    {"\x1b[1;4C", Movement::WordRight, true},
    {"\x1b""b", Movement::WordLeft, false},
    {"\x1b""f", Movement::WordRight, false},
    {"\x1b""B", Movement::WordLeft, true},
    {"\x1b""F", Movement::WordRight, true},
#endif
};
void SanitizePaste(std::string& text)
{
  std::erase_if(text,
                [](unsigned char ch)
                {
                  return (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') ||
                         ch == 127;
                });
}
}  // namespace

bool Editor::OnEvent(ftxui::Event event)
{
  using ftxui::Event;
  using ftxui::Mouse;
  if (event.is_mouse())
  {
    const auto& mouse = event.mouse();
    if (m_dragging && mouse.motion == Mouse::Released)
    {
      m_dragging = false;
      m_mouse_capture.reset();
      return true;
    }
    const bool inside = m_box.Contain(mouse.x, mouse.y);
    if (!inside && !m_dragging)
    {
      m_click_count = 0;
      return false;
    }
    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown)
    {
      m_click_count = 0;
      if (mouse.button == Mouse::WheelUp)
        m_top = m_top > 2 ? m_top - 3 : 0;
      else
        m_top += 3;
      m_follow_cursor = false;
      return true;
    }
    const bool start =
        mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed;
    const bool drag = m_dragging && mouse.button == Mouse::Left &&
                      mouse.motion == Mouse::Moved;
    if (start || drag)
    {
      if (!m_mouse_capture) m_mouse_capture = CaptureMouse(event);
      if (!m_mouse_capture) return false;
      TakeFocus();
      m_vertical_column.reset();
      const auto last_line = m_document->LineCount() - 1;
      if (drag && mouse.y < m_box.y_min && m_top) --m_top;
      if (drag && mouse.y > m_box.y_max && m_top < last_line) ++m_top;
      if (drag && mouse.x < m_box.x_min + GutterWidth())
        m_left = std::max(0, m_left - 1);
      if (drag && mouse.x > m_box.x_max) ++m_left;
      const int y = std::clamp(mouse.y, m_box.y_min, m_box.y_max);
      const auto line = std::min(
          last_line, m_top + static_cast<std::size_t>(y - m_box.y_min));
      const int column =
          std::max(0, mouse.x - m_box.x_min - GutterWidth() + m_left);
      const Position at{line, ByteColumn(line, column)};
      if (start)
      {
        const auto now = std::chrono::steady_clock::now();
        const bool consecutive =
            !mouse.shift && m_click_count && mouse.x == m_click_x &&
            mouse.y == m_click_y && at == m_click_position &&
            now - m_last_click <= std::chrono::milliseconds(500);
        m_click_count = consecutive ? m_click_count % 3 + 1 : 1;
        m_click_x = mouse.x;
        m_click_y = mouse.y;
        m_click_position = at;
        m_last_click = now;
        m_drag_unit = m_click_count == 2   ? DragUnit::Word
                      : m_click_count == 3 ? DragUnit::Line
                                           : DragUnit::Character;
        if (m_drag_unit != DragUnit::Character)
          m_drag_range = m_drag_unit == DragUnit::Word
                             ? m_document->WordRange(at)
                             : m_document->LineRange(line);
      }
      if (drag && at != m_click_position) m_click_count = 0;
      if (m_drag_unit == DragUnit::Character)
        m_document->SetCursor(at, drag || mouse.shift);
      else
      {
        const auto range = m_drag_unit == DragUnit::Word
                               ? m_document->WordRange(at)
                               : m_document->LineRange(line);
        const bool backward = range.first < m_drag_range.first;
        m_document->SetCursor(backward ? m_drag_range.second
                                       : m_drag_range.first);
        m_document->SetCursor(backward ? range.first : range.second, true);
      }
      m_dragging = true;
      m_follow_cursor = true;
      return true;
    }
    return false;
  }
  if (!Focused()) return false;
  m_click_count = 0;
  m_follow_cursor = true;
  if (event == Event::CtrlD)
  {
    m_vertical_column.reset();
    m_document->SelectNextOccurrence();
    return true;
  }
  if (event == Event::Escape && m_document->CursorCount() > 1)
  {
    m_document->ClearSecondarySelections();
    return true;
  }
  if (event == Event::CtrlA)
  {
    m_document->SelectAll();
    return true;
  }
  if (event == Event::CtrlC || event == Event::CtrlX)
  {
    m_clipboard_error.clear();
    if (m_document->HasSelection())
    {
      auto copied = m_clipboard->Write(m_document->ClipboardText());
      if (!copied)
        m_clipboard_error = copied.error();
      else if (event == Event::CtrlX)
      {
        m_vertical_column.reset();
        m_document->Insert({});
      }
    }
    return true;  // Even an empty selection must not become application exit.
  }
  if (event == Event::CtrlV)
  {
    m_clipboard_error.clear();
    auto pasted = m_clipboard->Read();
    if (!pasted)
      m_clipboard_error = pasted.error();
    else
    {
      SanitizePaste(*pasted);
      if (!pasted->empty()) m_document->Insert(*pasted);
    }
    return true;
  }
  auto move = [&](Movement movement, bool select)
  {
    if (movement == Movement::Up || movement == Movement::Down)
    {
      const auto cursor = m_document->Cursor();
      if (!m_vertical_column)
        m_vertical_column = CellColumn(cursor.line, cursor.column);
      m_document->Move(movement, select);
      const auto line = m_document->Cursor().line;
      m_document->SetCursor({line, ByteColumn(line, *m_vertical_column)},
                            select);
    }
    else
    {
      m_vertical_column.reset();
      m_document->Move(movement, select);
    }
  };
  for (const auto& binding : movement_keys)
    if (event.input() == binding.key)
    {
      move(binding.movement, binding.select);
      return true;
    }
  m_vertical_column.reset();
  if (event == Event::CtrlZ)
  {
    m_document->Undo();
    return true;
  }
  if (event == Event::CtrlY || event == Event::Special("\x1b[122;6u") ||
      event == Event::Special("\x1b[90;6u") ||
      event == Event::Special("\x1b[27;6;90~") ||
      event == Event::Special("\x1b[27;6;122~"))
  {
    m_document->Redo();
    return true;
  }
  if (event == Event::Return)
  {
    m_document->Enter();
    return true;
  }
  if (event == Event::Tab)
  {
    m_document->Tab();
    return true;
  }
  if (event == Event::Backspace)
  {
    m_document->Backspace();
    return true;
  }
  if (event == Event::Delete)
  {
    m_document->Delete();
    return true;
  }
  const bool select_page = event == Event::Special("\x1b[5;2~") ||
                           event == Event::Special("\x1b[6;2~");
  if (event == Event::PageUp || event == Event::PageDown || select_page)
  {
    const bool up =
        event == Event::PageUp || event == Event::Special("\x1b[5;2~");
    for (int i = 0; i < std::max(1, m_box.y_max - m_box.y_min); ++i)
      move(up ? Movement::Up : Movement::Down, select_page);
    return true;
  }
  if (event.is_character())
  {
    auto text = event.character();
    SanitizePaste(text);
    if (text.empty()) return false;
    if (text.size() == 1 && static_cast<unsigned char>(text[0]) < 128)
      m_document->Type(text[0]);
    else
      m_document->Insert(text);
    return true;
  }
  return false;
}
}  // namespace ptoolkit::editor
