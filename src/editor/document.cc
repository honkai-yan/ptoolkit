#include "ptoolkit_editor/document.h"

#include <algorithm>
#include <utility>

namespace ptoolkit::editor
{
namespace
{
bool Continuation(unsigned char ch) { return (ch & 0xc0) == 0x80; }
std::size_t Prev(std::string_view line, std::size_t column)
{
  if (!column) return 0;
  --column;
  while (column && Continuation(line[column])) --column;
  return column;
}
std::size_t Next(std::string_view line, std::size_t column)
{
  if (column == line.size()) return column;
  ++column;
  while (column < line.size() && Continuation(line[column])) ++column;
  return column;
}
Position Advance(Position start, std::string_view text)
{
  for (char ch : text)
    if (ch == '\n')
    {
      ++start.line;
      start.column = 0;
    }
    else
      ++start.column;
  return start;
}
std::string Normalize(std::string_view text)
{
  std::string normalized;
  normalized.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] == '\r')
    {
      normalized += '\n';
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
    }
    else
      normalized += text[i];
  }
  return normalized;
}
char Close(char ch)
{
  switch (ch)
  {
    case '(':
      return ')';
    case '{':
      return '}';
    case '[':
      return ']';
    case '"':
      return '"';
    default:
      return 0;
  }
}
bool Space(char ch) { return ch == ' ' || ch == '\t'; }
// VS Code default editor.wordSeparators (underscore remains part of a word).
int WordClass(unsigned char ch)
{
  if (Space(ch)) return 0;
  constexpr std::string_view separators = "`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?";
  return separators.find(ch) != std::string_view::npos ? 1 : 2;
}
}  // namespace

Document::Document(std::string_view text, std::string indent)
    : m_indent(indent.empty() ? "  " : std::move(indent))
{
  SetText(text);
}

void Document::SetText(std::string_view text)
{
  const auto normalized = Normalize(text);
  m_lines.clear();
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= normalized.size(); ++i)
    if (i == normalized.size() || normalized[i] == '\n')
    {
      m_lines.push_back({normalized.substr(begin, i - begin), ++m_version});
      begin = i + 1;
    }
  m_cursor = {};
  m_anchor.reset();
  m_secondary.clear();
  m_match_whole_word = false;
  m_history.clear();
  m_history_position = 0;
  m_state = m_saved_state = ++m_next_state;
  m_preferred_column.reset();
  BreakUndoGroup();
}

std::string Document::Text() const
{
  std::string text;
  std::size_t size = m_lines.size() - 1;
  for (const auto& line : m_lines) size += line.text.size();
  text.reserve(size);
  for (std::size_t i = 0; i < m_lines.size(); ++i)
  {
    if (i) text += '\n';
    text += m_lines[i].text;
  }
  return text;
}

Position Document::ClampPosition(Position cursor) const
{
  cursor.line = std::min(cursor.line, m_lines.size() - 1);
  const auto& line = Line(cursor.line);
  cursor.column = std::min(cursor.column, line.size());
  while (cursor.column < line.size() && cursor.column &&
         Continuation(line[cursor.column]))
    --cursor.column;
  return cursor;
}
void Document::SetCursor(Position cursor, bool select)
{
  ClearSecondarySelections();
  if (select && !m_anchor) m_anchor = m_cursor;
  if (!select) m_anchor.reset();
  m_cursor = ClampPosition(cursor);
  m_preferred_column.reset();
  BreakUndoGroup();
}

void Document::WordLeft()
{
  if (!m_cursor.column && m_cursor.line)
  {
    --m_cursor.line;
    m_cursor.column = Line(m_cursor.line).size();
  }
  const auto& line = Line(m_cursor.line);
  auto find = [&](std::size_t end)
  {
    while (end && WordClass(line[end - 1]) == 0) --end;
    const auto stop = end;
    if (end)
    {
      const int type = WordClass(line[end - 1]);
      while (end && WordClass(line[end - 1]) == type) --end;
    }
    return std::pair{end, stop};
  };
  auto [begin, end] = find(m_cursor.column);
  // WordStartFast skips a single separator adjoining the preceding regular
  // word.
  if (end - begin == 1 && WordClass(line[begin]) == 1 && begin &&
      WordClass(line[begin - 1]) == 2)
    begin = find(begin).first;
  m_cursor.column = begin;
}

void Document::WordRight()
{
  if (m_cursor.column == Line(m_cursor.line).size() &&
      m_cursor.line + 1 < m_lines.size())
  {
    ++m_cursor.line;
    m_cursor.column = 0;
  }
  const auto& line = Line(m_cursor.line);
  auto find = [&](std::size_t begin)
  {
    while (begin < line.size() && WordClass(line[begin]) == 0) ++begin;
    const auto start = begin;
    if (begin < line.size())
    {
      const int type = WordClass(line[begin]);
      while (begin < line.size() && WordClass(line[begin]) == type) ++begin;
    }
    return std::pair{start, begin};
  };
  auto [begin, end] = find(m_cursor.column);
  // VS Code WordEnd skips a single separator followed by a regular word.
  if (end - begin == 1 && WordClass(line[begin]) == 1 && end < line.size() &&
      WordClass(line[end]) == 2)
    end = find(end).second;
  m_cursor.column = end;
}

std::pair<Position, Position> Document::Selection() const
{
  if (!m_anchor) return {m_cursor, m_cursor};
  return {std::min(*m_anchor, m_cursor), std::max(*m_anchor, m_cursor)};
}
std::string Document::SelectedText() const
{
  const auto [begin, end] = Selection();
  return Range(begin, end);
}
void Document::SelectAll()
{
  ClearSecondarySelections();
  m_anchor = Position{};
  m_cursor = {m_lines.size() - 1, m_lines.back().text.size()};
  m_preferred_column.reset();
  BreakUndoGroup();
}
std::pair<Position, Position> Document::WordRange(Position at) const
{
  at.line = std::min(at.line, LineCount() - 1);
  const auto& line = Line(at.line);
  if (line.empty()) return {{at.line, 0}, {at.line, 0}};
  auto begin = std::min(at.column, line.size() - 1);
  while (begin && Continuation(line[begin])) --begin;
  auto end = Next(line, begin);
  const auto type = WordClass(line[begin]);
  while (begin && WordClass(line[Prev(line, begin)]) == type)
    begin = Prev(line, begin);
  while (end < line.size() && WordClass(line[end]) == type)
    end = Next(line, end);
  return {{at.line, begin}, {at.line, end}};
}
std::pair<Position, Position> Document::LineRange(std::size_t line) const
{
  line = std::min(line, LineCount() - 1);
  // Include the newline so copying/deleting a whole line behaves naturally.
  return {{line, 0},
          line + 1 < LineCount() ? Position{line + 1, 0}
                                 : Position{line, Line(line).size()}};
}
void Document::ClearSelection()
{
  ClearSecondarySelections();
  m_anchor.reset();
  BreakUndoGroup();
}

void Document::Move(Movement movement, bool select)
{
  ClearSecondarySelections();
  BreakUndoGroup();
  if (!select && HasSelection() &&
      (movement == Movement::Left || movement == Movement::Right))
  {
    const auto [begin, end] = Selection();
    SetCursor(movement == Movement::Left ? begin : end);
    return;
  }
  if (select && !m_anchor) m_anchor = m_cursor;
  if (!select) m_anchor.reset();
  if (movement == Movement::Up || movement == Movement::Down)
  {
    if (!m_preferred_column) m_preferred_column = m_cursor.column;
    if (movement == Movement::Up && m_cursor.line) --m_cursor.line;
    if (movement == Movement::Down && m_cursor.line + 1 < m_lines.size())
      ++m_cursor.line;
    const auto& line = Line(m_cursor.line);
    m_cursor.column = std::min(*m_preferred_column, line.size());
    while (m_cursor.column < line.size() && m_cursor.column &&
           Continuation(line[m_cursor.column]))
      --m_cursor.column;
    return;
  }
  m_preferred_column.reset();
  const auto& line = Line(m_cursor.line);
  switch (movement)
  {
    case Movement::Left:
      if (m_cursor.column)
        m_cursor.column = Prev(line, m_cursor.column);
      else if (m_cursor.line)
      {
        --m_cursor.line;
        m_cursor.column = Line(m_cursor.line).size();
      }
      break;
    case Movement::Right:
      if (m_cursor.column < line.size())
        m_cursor.column = Next(line, m_cursor.column);
      else if (m_cursor.line + 1 < m_lines.size())
      {
        ++m_cursor.line;
        m_cursor.column = 0;
      }
      break;
    case Movement::Home:
      m_cursor.column = 0;
      break;
    case Movement::End:
      m_cursor.column = line.size();
      break;
    case Movement::DocumentHome:
      m_cursor = {};
      break;
    case Movement::DocumentEnd:
      m_cursor = {m_lines.size() - 1, m_lines.back().text.size()};
      break;
    case Movement::WordLeft:
      WordLeft();
      break;
    case Movement::WordRight:
      WordRight();
      break;
    default:
      break;
  }
}

std::string Document::Range(Position begin, Position end) const
{
  if (begin.line == end.line)
    return Line(begin.line).substr(begin.column, end.column - begin.column);
  std::string text = Line(begin.line).substr(begin.column);
  for (auto line = begin.line + 1; line < end.line; ++line)
    text += '\n' + Line(line);
  return text + '\n' + Line(end.line).substr(0, end.column);
}

void Document::ReplaceRaw(Position begin, Position end,
                          std::string_view inserted)
{
  std::vector<LineData> lines;
  const auto prefix = Line(begin.line).substr(0, begin.column);
  const auto suffix = Line(end.line).substr(end.column);
  std::size_t start = 0;
  for (std::size_t i = 0; i <= inserted.size(); ++i)
    if (i == inserted.size() || inserted[i] == '\n')
    {
      std::string text(inserted.substr(start, i - start));
      if (lines.empty()) text.insert(0, prefix);
      if (i == inserted.size()) text += suffix;
      lines.push_back({std::move(text), ++m_version});
      start = i + 1;
    }
  // Avoid vector shifts entirely for the common one-line edit.
  if (begin.line == end.line && lines.size() == 1)
    m_lines[begin.line] = std::move(lines[0]);
  else
  {
    auto at = m_lines.erase(m_lines.begin() + begin.line,
                            m_lines.begin() + end.line + 1);
    m_lines.insert(at, std::make_move_iterator(lines.begin()),
                   std::make_move_iterator(lines.end()));
  }
}

void Document::Replace(Position begin, Position end, std::string inserted,
                       Position after, bool merge)
{
  if (begin == end && inserted.empty()) return;
  auto removed = Range(begin, end);
  const auto before = m_cursor;
  const auto before_anchor = m_anchor;
  auto before_secondary = m_secondary;
  ReplaceRaw(begin, end, inserted);
  m_cursor = after;
  m_anchor.reset();
  m_secondary.clear();
  m_preferred_column.reset();
  m_history.resize(m_history_position);
  const auto previous_state = m_state;
  m_state = ++m_next_state;
  if (merge && m_merge_typing && !m_history.empty() && removed.empty() &&
      m_history.back().parts.empty() && m_history.back().removed.empty() &&
      m_history.back().after == before &&
      Advance(m_history.back().at, m_history.back().inserted) == begin)
  {
    auto& edit = m_history.back();
    edit.inserted += inserted;
    edit.after = after;
    edit.after_state = m_state;
  }
  else
  {
    Edit edit{begin,
              before,
              after,
              std::move(removed),
              std::move(inserted),
              previous_state,
              m_state,
              before_anchor};
    edit.before_match_whole_word = m_match_whole_word;
    edit.before_secondary = std::move(before_secondary);
    m_history.push_back(std::move(edit));
  }
  m_history_position = m_history.size();
  m_merge_typing = merge;
}

bool Document::InString() const
{
  bool quoted = false, escape = false;
  const auto& line = Line(m_cursor.line);
  for (std::size_t i = 0; i < m_cursor.column; ++i)
  {
    const char ch = line[i];
    if (escape)
      escape = false;
    else if (quoted && ch == '\\')
      escape = true;
    else if (ch == '"')
      quoted = !quoted;
  }
  return quoted;
}

void Document::ReplaceText(std::string_view text)
{
  const auto normalized = Normalize(text);
  if (normalized == Text()) return;
  BreakUndoGroup();
  const auto before = m_cursor;
  const auto anchor = m_anchor;
  const auto secondary = m_secondary;
  Replace({}, {LineCount() - 1, Line(LineCount() - 1).size()}, normalized, {});
  m_cursor = ClampPosition(before);
  m_anchor = anchor ? std::optional(ClampPosition(*anchor)) : std::nullopt;
  for (auto state : secondary)
  {
    state.cursor = ClampPosition(state.cursor);
    if (state.anchor) state.anchor = ClampPosition(*state.anchor);
    const auto begin =
        std::min(state.cursor, state.anchor.value_or(state.cursor));
    const auto end =
        std::max(state.cursor, state.anchor.value_or(state.cursor));
    if (std::pair{begin, end} == Selection()) continue;
    if (std::none_of(
            m_secondary.begin(), m_secondary.end(),
            [&](const auto& s)
            {
              return std::min(s.cursor, s.anchor.value_or(s.cursor)) == begin &&
                     std::max(s.cursor, s.anchor.value_or(s.cursor)) == end;
            }))
      m_secondary.push_back(state);
  }
  auto& edit = m_history.back();
  edit.after = m_cursor;
  edit.after_anchor = m_anchor;
  edit.after_secondary = m_secondary;
  m_match_whole_word = false;
}

void Document::Insert(std::string_view text)
{
  if (ApplyMultiple([&] { Insert(text); })) return;
  auto normalized = Normalize(text);
  const auto [begin, end] = Selection();
  const auto after = Advance(begin, normalized);
  Replace(begin, end, std::move(normalized), after);
}

void Document::Type(char ch)
{
  if (ApplyMultiple([&] { Type(ch); })) return;
  if (HasSelection())
  {
    Insert(std::string(1, ch));
    return;
  }
  const auto& line = Line(m_cursor.line);
  const char next = m_cursor.column < line.size() ? line[m_cursor.column] : 0;
  bool escaped = false;
  for (auto at = m_cursor.column; at && line[at - 1] == '\\'; --at)
    escaped = !escaped;
  const bool quoted = InString();
  if (next == ch && ((ch == '"' && quoted && !escaped) ||
                     (!quoted && (ch == ')' || ch == '}' || ch == ']'))))
  {
    Move(Movement::Right);
    return;
  }
  const auto close = Close(ch);
  if (close && !quoted &&
      (!next || Space(next) || next == ',' || next == ':' || next == '}' ||
       next == ']' || next == ')'))
  {
    Replace(m_cursor, m_cursor, std::string{ch, close},
            {m_cursor.line, m_cursor.column + 1});
    return;
  }
  if (!quoted && (ch == '}' || ch == ']') &&
      std::all_of(line.begin(), line.begin() + m_cursor.column, Space))
  {
    const auto remove = std::min(m_indent.size(), m_cursor.column);
    const auto begin = Position{m_cursor.line, m_cursor.column - remove};
    Replace(begin, m_cursor, std::string(1, ch),
            {begin.line, begin.column + 1});
    return;
  }
  Replace(m_cursor, m_cursor, std::string(1, ch),
          {m_cursor.line, m_cursor.column + 1}, true);
}

void Document::Enter()
{
  if (ApplyMultiple([&] { Enter(); })) return;
  if (HasSelection())
  {
    const auto [begin, end] = Selection();
    const auto& line = Line(begin.line);
    const auto indent =
        line.substr(0, std::min(line.find_first_not_of(" \t"), begin.column));
    Replace(begin, end, "\n" + indent, {begin.line + 1, indent.size()});
    return;
  }
  const auto& line = Line(m_cursor.line);
  const auto first = line.find_first_not_of(" \t");
  std::string indent = line.substr(0, std::min(first, m_cursor.column));
  auto prefix = line.substr(0, m_cursor.column);
  while (!prefix.empty() && Space(prefix.back())) prefix.pop_back();
  const char opener = prefix.empty() ? 0 : prefix.back();
  const bool increase =
      !InString() && (opener == '{' || opener == '[' || opener == '(');
  const auto inner = indent + (increase ? m_indent : "");
  const auto next = line.find_first_not_of(" \t", m_cursor.column);
  const bool between =
      increase && next != std::string::npos && line[next] == Close(opener);
  auto end = m_cursor;
  std::string inserted = "\n" + inner;
  if (between)
  {
    end.column = next;
    inserted += "\n" + indent;
  }
  Replace(m_cursor, end, std::move(inserted),
          {m_cursor.line + 1, inner.size()});
}

void Document::Tab() { Insert(m_indent); }
void Document::Backspace()
{
  if (ApplyMultiple([&] { Backspace(); })) return;
  if (HasSelection())
  {
    const auto [begin, end] = Selection();
    Replace(begin, end, {}, begin);
    return;
  }
  auto begin = m_cursor, end = m_cursor;
  const auto& line = Line(m_cursor.line);
  if (begin.column)
  {
    begin.column = Prev(line, begin.column);
    if (!InString() && end.column < line.size() &&
        Close(line[begin.column]) == line[end.column])
      ++end.column;
    else if (line[begin.column] == '"' && end.column < line.size() &&
             line[end.column] == '"')
      ++end.column;
  }
  else if (begin.line)
  {
    --begin.line;
    begin.column = Line(begin.line).size();
  }
  Replace(begin, end, {}, begin);
}
void Document::Delete()
{
  if (ApplyMultiple([&] { Delete(); })) return;
  if (HasSelection())
  {
    const auto [begin, end] = Selection();
    Replace(begin, end, {}, begin);
    return;
  }
  auto end = m_cursor;
  if (end.column < Line(end.line).size())
    end.column = Next(Line(end.line), end.column);
  else if (end.line + 1 < m_lines.size())
  {
    ++end.line;
    end.column = 0;
  }
  Replace(m_cursor, end, {}, m_cursor);
}
bool Document::Undo()
{
  BreakUndoGroup();
  if (!m_history_position) return false;
  const auto& edit = m_history[--m_history_position];
  if (edit.parts.empty())
    ReplaceRaw(edit.at, Advance(edit.at, edit.inserted), edit.removed);
  else
    for (auto part = edit.parts.rbegin(); part != edit.parts.rend(); ++part)
      ReplaceRaw(part->at, Advance(part->at, part->inserted), part->removed);
  m_cursor = edit.before;
  m_anchor = edit.before_anchor;
  m_secondary = edit.before_secondary;
  m_match_whole_word = edit.before_match_whole_word;
  m_state = edit.before_state;
  m_preferred_column.reset();
  return true;
}
bool Document::Redo()
{
  BreakUndoGroup();
  if (m_history_position == m_history.size()) return false;
  const auto& edit = m_history[m_history_position++];
  if (edit.parts.empty())
    ReplaceRaw(edit.at, Advance(edit.at, edit.removed), edit.inserted);
  else
    for (const auto& part : edit.parts)
      ReplaceRaw(part.at, Advance(part.at, part.removed), part.inserted);
  m_cursor = edit.after;
  m_anchor = edit.after_anchor;
  m_secondary = edit.after_secondary;
  m_match_whole_word = false;
  m_state = edit.after_state;
  m_preferred_column.reset();
  return true;
}
void Document::MarkSaved()
{
  m_saved_state = m_state;
  BreakUndoGroup();
}
}  // namespace ptoolkit::editor
