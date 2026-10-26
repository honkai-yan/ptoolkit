#pragma once

#include "ptoolkit_editor/clipboard.h"
#include "ptoolkit_editor/document.h"
#include "ptoolkit_editor/json_highlight.h"

#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

namespace ptoolkit::editor
{
// A normal FTXUI child component; never owns a screen, event loop or file.
class Editor : public ftxui::ComponentBase
{
public:
  explicit Editor(
      std::shared_ptr<Document> document = std::make_shared<Document>(),
      std::shared_ptr<Clipboard> clipboard = {});
  void SetDocument(std::shared_ptr<Document> document);
  std::shared_ptr<Document> GetDocument() const { return m_document; }
  const std::string& ClipboardError() const { return m_clipboard_error; }
  bool Focusable() const override { return true; }
  bool OnEvent(ftxui::Event event) override;
  ftxui::Element OnRender() override;

private:
  class EditorNode;
  struct Glyph
  {
    std::string text;
    std::size_t begin, end;
    int cell, width;
    TokenKind kind;
  };
  struct CachedLine
  {
    std::uint64_t version = 0;
    std::vector<Glyph> glyphs;
    int width = 0;
  };
  const CachedLine& GetLine(std::size_t line);
  int CellColumn(std::size_t line, std::size_t byte);
  std::size_t ByteColumn(std::size_t line, int cell);
  int GutterWidth() const;

  std::shared_ptr<Document> m_document;
  std::shared_ptr<Clipboard> m_clipboard;
  std::string m_clipboard_error;
  ftxui::CapturedMouse m_mouse_capture;
  bool m_dragging = false;
  int m_click_count = 0;
  int m_click_x = 0, m_click_y = 0;
  std::chrono::steady_clock::time_point m_last_click;
  Position m_click_position;
  enum class DragUnit
  {
    Character,
    Word,
    Line
  };
  DragUnit m_drag_unit = DragUnit::Character;
  std::pair<Position, Position> m_drag_range;
  std::unordered_map<std::size_t, CachedLine> m_cache;
  ftxui::Box m_box;
  std::size_t m_top = 0;
  int m_left = 0;
  bool m_follow_cursor = true;
  std::optional<int> m_vertical_column;
};
}  // namespace ptoolkit::editor
