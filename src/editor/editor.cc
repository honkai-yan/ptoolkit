#include "ptoolkit_editor/editor.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

namespace ptoolkit::editor
{
Editor::Editor(std::shared_ptr<Document> document,
               std::shared_ptr<Clipboard> clipboard, bool json_highlight)
    : m_clipboard(clipboard ? std::move(clipboard) : MakeSystemClipboard()),
      m_json_highlight(json_highlight)
{
  SetDocument(std::move(document));
}
void Editor::SetDocument(std::shared_ptr<Document> document)
{
  if (document == m_document) return;
  m_document = document ? std::move(document) : std::make_shared<Document>();
  m_cache.clear();
  m_mouse_capture.reset();
  m_dragging = false;
  m_click_count = 0;
  m_drag_unit = DragUnit::Character;
  m_clipboard_error.clear();
  m_vertical_column.reset();
  m_top = 0;
  m_left = 0;
  m_follow_cursor = true;
}
int Editor::GutterWidth() const
{
  return static_cast<int>(std::to_string(m_document->LineCount()).size()) + 2;
}

const Editor::CachedLine& Editor::GetLine(std::size_t number)
{
  auto& cached = m_cache[number];
  if (cached.version == m_document->LineVersion(number)) return cached;
  cached = {};
  cached.version = m_document->LineVersion(number);
  const auto& line = m_document->Line(number);
  // An empty token list renders every glyph as plain text.
  const auto tokens =
      m_json_highlight ? HighlightJsonLine(line) : std::vector<Token>{};
  std::size_t offset = 0, token = 0;
  for (auto& glyph : ftxui::Utf8ToGlyphs(line))
  {
    if (glyph.empty()) continue;
    while (token + 1 < tokens.size() && offset >= tokens[token].end) ++token;
    const auto end = offset + glyph.size();
    const int width =
        glyph == "\t" ? 4 - cached.width % 4 : ftxui::string_width(glyph);
    const auto kind = tokens.empty() ? TokenKind::Text : tokens[token].kind;
    cached.glyphs.push_back(
        {std::move(glyph), offset, end, cached.width, width, kind});
    offset = end;
    cached.width += width;
  }
  return cached;
}

int Editor::CellColumn(std::size_t line, std::size_t byte)
{
  const auto& cached = GetLine(line);
  for (const auto& glyph : cached.glyphs)
    if (glyph.begin >= byte) return glyph.cell;
  return cached.width;
}
std::size_t Editor::ByteColumn(std::size_t line, int cell)
{
  for (const auto& glyph : GetLine(line).glyphs)
    if (glyph.cell + glyph.width > cell) return glyph.begin;
  return m_document->Line(line).size();
}

class Editor::EditorNode : public ftxui::Node
{
public:
  explicit EditorNode(Editor& editor) : m_editor(editor) {}
  void ComputeRequirement() override
  {
    requirement_ = {};
    requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
    requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    if (m_editor.Focused())
    {
      requirement_.focused.enabled = true;
      requirement_.focused.node = this;
      requirement_.focused.cursor_shape = ftxui::Screen::Cursor::BarBlinking;
    }
  }
  void SetBox(ftxui::Box box) override
  {
    Node::SetBox(box);
    m_editor.m_box = box;
  }
  void Render(ftxui::Screen& screen) override
  {
    using ftxui::Color;
    auto& editor = m_editor;
    const auto bounds = ftxui::Box::Intersection(box_, screen.stencil);
    if (bounds.IsEmpty()) return;
    const int height = box_.y_max - box_.y_min + 1;
    const int gutter = editor.GutterWidth();
    const int width = std::max(1, box_.x_max - box_.x_min + 1 - gutter);
    const auto cursor = editor.m_document->Cursor();
    const int column = editor.CellColumn(cursor.line, cursor.column);
    if (editor.m_follow_cursor)
    {
      if (cursor.line < editor.m_top) editor.m_top = cursor.line;
      if (cursor.line >= editor.m_top + height)
        editor.m_top = cursor.line - height + 1;
      if (column < editor.m_left) editor.m_left = column;
      if (column >= editor.m_left + width) editor.m_left = column - width + 1;
    }
    editor.m_top =
        std::min(editor.m_top, editor.m_document->LineCount() >
                                       static_cast<std::size_t>(height)
                                   ? editor.m_document->LineCount() - height
                                   : 0);
    const auto foreground = JsonTokenColor(TokenKind::Text);
    const auto selections = editor.m_document->Selections();
    for (int y = bounds.y_min; y <= bounds.y_max; ++y)
    {
      const auto line = editor.m_top + y - box_.y_min;
      const auto background = line == cursor.line && editor.Focused()
                                  ? Color::RGB(0x25, 0x25, 0x26)
                                  : Color::RGB(0x1e, 0x1e, 0x1e);
      for (int x = bounds.x_min; x <= bounds.x_max; ++x)
      {
        auto& cell = screen.CellAt(x, y);
        cell = ftxui::Cell();
        cell.character = " ";
        cell.background_color = background;
        cell.foreground_color = foreground;
      }
      if (line >= editor.m_document->LineCount()) continue;
      auto selection =
          std::lower_bound(selections.begin(), selections.end(), line,
                           [](const auto& range, std::size_t row)
                           { return range.second.line < row; });
      for (; selection != selections.end() && selection->first.line <= line;
           ++selection)
      {
        const auto [selection_begin, selection_end] = *selection;
        if (selection_begin == selection_end) continue;
        const int first = line == selection_begin.line
                              ? editor.CellColumn(line, selection_begin.column)
                              : 0;
        const int last = line == selection_end.line
                             ? editor.CellColumn(line, selection_end.column)
                             : editor.GetLine(line).width + 1;
        const int text_start = box_.x_min + gutter;
        for (int x = std::max({bounds.x_min, text_start,
                               text_start + first - editor.m_left});
             x <= std::min(bounds.x_max, text_start + last - editor.m_left - 1);
             ++x)
          screen.CellAt(x, y).background_color =
              editor.Focused() ? Color::RGB(0x26, 0x4f, 0x78)
                               : Color::RGB(0x3a, 0x3d, 0x41);
      }
      const auto number = std::to_string(line + 1);
      const int start =
          box_.x_min + gutter - 1 - static_cast<int>(number.size());
      for (int i = 0; i < static_cast<int>(number.size()); ++i)
        if (bounds.Contain(start + i, y))
        {
          auto& cell = screen.CellAt(start + i, y);
          cell.character = std::string(1, number[i]);
          cell.foreground_color = Color::RGB(0x85, 0x85, 0x85);
        }
      for (const auto& glyph : editor.GetLine(line).glyphs)
      {
        const int x = box_.x_min + gutter + glyph.cell - editor.m_left;
        if (x < box_.x_min + gutter || x < bounds.x_min) continue;
        if (x + glyph.width - 1 > bounds.x_max) break;
        if (glyph.text == "\t") continue;
        auto& cell = screen.CellAt(x, y);
        cell.character = glyph.text;
        cell.foreground_color = JsonTokenColor(glyph.kind);
        if (glyph.width == 2) screen.CellAt(x + 1, y).character = "";
      }
      // The terminal has one real caret; draw extra collapsed carets as bars.
      if (editor.Focused() && editor.m_document->CursorCount() > 1)
        for (const auto& [begin, end] : selections)
          if (begin == end && begin != cursor && begin.line == line)
          {
            const int x = box_.x_min + gutter +
                          editor.CellColumn(line, begin.column) - editor.m_left;
            if (x >= box_.x_min + gutter && bounds.Contain(x, y))
              screen.CellAt(x, y).inverted = true;
          }
    }
    // Keep the cache bounded to a small neighborhood of the visible viewport.
    std::erase_if(editor.m_cache,
                  [&](const auto& item)
                  {
                    return item.first + height < editor.m_top ||
                           item.first > editor.m_top + 2 * height;
                  });
    if (editor.Focused())
    {
      const int x = box_.x_min + gutter + column - editor.m_left;
      const int y = box_.y_min + static_cast<int>(cursor.line) -
                    static_cast<int>(editor.m_top);
      if (bounds.Contain(x, y))
        screen.SetCursor({x, y, ftxui::Screen::Cursor::BarBlinking});
      else
        screen.SetCursor({0, 0, ftxui::Screen::Cursor::Hidden});
    }
  }

private:
  Editor& m_editor;
};
ftxui::Element Editor::OnRender()
{
  return std::make_shared<EditorNode>(*this);
}
}  // namespace ptoolkit::editor
