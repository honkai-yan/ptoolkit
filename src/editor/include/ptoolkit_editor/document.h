#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ptoolkit::editor
{
// Zero-based line and UTF-8 byte column; always located on a character
// boundary.
struct Position
{
  std::size_t line = 0;
  std::size_t column = 0;
  auto operator<=>(const Position&) const = default;
};

enum class Movement
{
  Left,
  Right,
  Up,
  Down,
  WordLeft,
  WordRight,
  Home,
  End,
  DocumentHome,
  DocumentEnd
};

// No terminal, filesystem, JSON parser, or application dependencies.
// Single-line changes touch one line; history stores reversible range edits.
class Document
{
public:
  explicit Document(std::string_view text = {}, std::string indent = "  ");
  void SetText(std::string_view text);
  // Undoable whole-document replacement; clamp and retain all
  // carets/selections.
  void ReplaceText(std::string_view text);
  std::string Text() const;
  std::size_t LineCount() const { return m_lines.size(); }
  const std::string& Line(std::size_t line) const
  {
    return m_lines.at(line).text;
  }
  std::uint64_t LineVersion(std::size_t line) const
  {
    return m_lines.at(line).version;
  }
  Position Cursor() const { return m_cursor; }
  void SetCursor(Position cursor, bool select = false);
  void Move(Movement movement, bool select = false);
  bool HasSelection() const;
  std::pair<Position, Position> Selection() const;
  std::vector<std::pair<Position, Position>> Selections() const;
  std::string SelectedText() const;
  std::string ClipboardText() const;
  void SelectNextOccurrence();
  void ClearSecondarySelections();
  std::size_t CursorCount() const { return 1 + m_secondary.size(); }
  std::pair<Position, Position> WordRange(Position at) const;
  std::pair<Position, Position> LineRange(std::size_t line) const;
  void SelectAll();
  void ClearSelection();
  void Insert(
      std::string_view text);  // Paste literally, as one undo transaction.
  void Type(char ch);          // Auto-pair and skip closing delimiters.
  void Enter();
  void Tab();
  void Backspace();
  void Delete();
  bool Undo();
  bool Redo();
  bool Modified() const { return m_state != m_saved_state; }
  void MarkSaved();
  void BreakUndoGroup() { m_merge_typing = false; }

private:
  struct SelectionState
  {
    Position cursor;
    std::optional<Position> anchor;
  };
  struct LineData
  {
    std::string text;
    std::uint64_t version;
  };
  struct Edit
  {
    Position at, before, after;
    std::string removed, inserted;
    std::uint64_t before_state, after_state;
    std::optional<Position> before_anchor;
    std::optional<Position> after_anchor;
    std::vector<Edit> parts;
    std::vector<SelectionState> before_secondary, after_secondary;
    bool before_match_whole_word = false;
  };
  bool ApplyMultiple(const std::function<void()>& action);
  Position ClampPosition(Position cursor) const;
  void Replace(Position begin, Position end, std::string inserted,
               Position after, bool merge = false);
  void ReplaceRaw(Position begin, Position end, std::string_view inserted);
  std::string Range(Position begin, Position end) const;
  bool InString() const;
  void WordLeft();
  void WordRight();

  std::vector<LineData> m_lines;
  Position m_cursor;
  std::optional<Position> m_anchor;
  std::vector<SelectionState> m_secondary;
  bool m_applying_multiple = false;
  bool m_match_whole_word = false;
  std::string m_indent;
  std::optional<std::size_t> m_preferred_column;
  std::vector<Edit> m_history;
  std::size_t m_history_position = 0;
  std::uint64_t m_version = 0, m_state = 0, m_next_state = 0, m_saved_state = 0;
  bool m_merge_typing = false;
};
}  // namespace ptoolkit::editor
