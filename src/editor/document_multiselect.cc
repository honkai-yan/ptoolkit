#include "ptoolkit_editor/document.h"

#include <algorithm>
#include <numeric>

namespace ptoolkit::editor
{
namespace
{
Position Advance(Position at, std::string_view text)
{
  for (char ch : text)
    if (ch == '\n')
    {
      ++at.line;
      at.column = 0;
    }
    else
      ++at.column;
  return at;
}
// Move a previously processed (higher) caret through a subsequent lower edit.
Position Transform(Position at, Position begin, Position end, Position after)
{
  if (at < begin) return at;
  if (at < end) return after;
  if (at.line == end.line)
    return {after.line, after.column + at.column - end.column};
  return {after.line + at.line - end.line, at.column};
}
}  // namespace

bool Document::HasSelection() const
{
  if (m_anchor && *m_anchor != m_cursor) return true;
  return std::any_of(m_secondary.begin(), m_secondary.end(), [](const auto& s)
                     { return s.anchor && *s.anchor != s.cursor; });
}

std::vector<std::pair<Position, Position>> Document::Selections() const
{
  std::vector<std::pair<Position, Position>> ranges;
  ranges.reserve(CursorCount());
  ranges.push_back(Selection());
  for (const auto& s : m_secondary)
  {
    const auto anchor = s.anchor.value_or(s.cursor);
    ranges.emplace_back(std::min(anchor, s.cursor), std::max(anchor, s.cursor));
  }
  std::sort(ranges.begin(), ranges.end());
  return ranges;
}

std::string Document::ClipboardText() const
{
  std::string text;
  bool first = true;
  for (const auto& [begin, end] : Selections())
    if (begin != end)
    {
      if (!first) text += '\n';
      text += Range(begin, end);
      first = false;
    }
  return text;
}

void Document::ClearSecondarySelections()
{
  m_secondary.clear();
  m_match_whole_word = false;
}

void Document::SelectNextOccurrence()
{
  BreakUndoGroup();
  m_preferred_column.reset();
  if (!m_anchor || *m_anchor == m_cursor)
  {
    ClearSecondarySelections();
    const auto [begin, end] = WordRange(m_cursor);
    m_anchor = begin;
    m_cursor = end;
    m_match_whole_word = true;
    return;
  }
  const auto needle = SelectedText();
  if (needle.empty()) return;
  std::vector<std::string_view> pieces;
  for (std::size_t at = 0;;)
  {
    const auto end = needle.find('\n', at);
    pieces.push_back(std::string_view(needle).substr(at, end - at));
    if (end == std::string::npos) break;
    at = end + 1;
  }
  const auto occupied = Selections();
  const auto start = Selection().second;
  // Search stored lines directly, without flattening/copying the document.
  const auto find =
      [&](Position from,
          Position limit) -> std::optional<std::pair<Position, Position>>
  {
    for (auto row = from.line; row < LineCount() && row <= limit.line; ++row)
    {
      const auto& line = Line(row);
      const auto column = row == from.line ? from.column : 0;
      for (auto at = pieces.front().empty() ? line.size()
                                            : line.find(pieces.front(), column);
           at != std::string::npos && at <= line.size();
           at = pieces.front().empty() ? std::string::npos
                                       : line.find(pieces.front(), at + 1))
      {
        const Position begin{row, at};
        if (begin >= limit) break;
        Position finish{row, at + pieces.front().size()};
        if (pieces.size() > 1)
        {
          if (finish.column != line.size() || row + pieces.size() > LineCount())
            continue;
          bool matches = true;
          for (std::size_t i = 1; i < pieces.size(); ++i)
          {
            const auto& next_line = Line(row + i);
            if (i + 1 == pieces.size() ? !next_line.starts_with(pieces[i])
                                       : next_line != pieces[i])
            {
              matches = false;
              break;
            }
          }
          if (!matches) continue;
          finish = {row + pieces.size() - 1, pieces.back().size()};
        }
        // Never select half of a UTF-8 code point.
        if ((at < line.size() &&
             (static_cast<unsigned char>(line[at]) & 0xc0) == 0x80) ||
            (finish.column < Line(finish.line).size() &&
             (static_cast<unsigned char>(Line(finish.line)[finish.column]) &
              0xc0) == 0x80))
          continue;
        if (m_match_whole_word && WordRange(begin) != std::pair{begin, finish})
          continue;
        const auto next = std::lower_bound(
            occupied.begin(), occupied.end(), begin,
            [](const auto& range, Position p) { return range.second <= p; });
        if (next == occupied.end() || next->first >= finish)
          return std::pair{begin, finish};
      }
    }
    return {};
  };
  auto next = find(start, {LineCount() - 1, Line(LineCount() - 1).size()});
  if (!next) next = find({}, start);
  if (!next) return;
  m_secondary.push_back({m_cursor, m_anchor});
  m_anchor = next->first;
  m_cursor = next->second;
}

bool Document::ApplyMultiple(const std::function<void()>& action)
{
  if (m_applying_multiple || m_secondary.empty()) return false;
  BreakUndoGroup();
  Edit transaction{};
  transaction.before = m_cursor;
  transaction.before_anchor = m_anchor;
  transaction.before_secondary = m_secondary;
  transaction.before_match_whole_word = m_match_whole_word;
  transaction.before_state = m_state;
  auto states = m_secondary;
  states.push_back({m_cursor, m_anchor});
  const auto primary = states.size() - 1;
  std::vector<std::size_t> order(states.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](auto a, auto b)
            {
              return std::min(states[a].cursor,
                              states[a].anchor.value_or(states[a].cursor)) >
                     std::min(states[b].cursor,
                              states[b].anchor.value_or(states[b].cursor));
            });

  // Reuse all single-caret editing behavior, but collect its deltas into one
  // history entry. No full-document snapshots, including for undo/redo.
  auto history = std::move(m_history);
  const auto history_position = m_history_position;
  m_history = {};
  m_history_position = 0;
  m_secondary.clear();
  m_applying_multiple = true;
  std::vector<std::size_t> processed;
  for (const auto index : order)
  {
    m_cursor = states[index].cursor;
    m_anchor = states[index].anchor;
    BreakUndoGroup();
    const auto first_edit = m_history.size();
    action();
    for (auto i = first_edit; i < m_history.size(); ++i)
    {
      const auto& edit = m_history[i];
      const auto end = Advance(edit.at, edit.removed);
      const auto after = Advance(edit.at, edit.inserted);
      for (const auto done : processed)
      {
        states[done].cursor =
            Transform(states[done].cursor, edit.at, end, after);
        if (states[done].anchor)
          states[done].anchor =
              Transform(*states[done].anchor, edit.at, end, after);
      }
    }
    states[index] = {m_cursor, m_anchor};
    processed.push_back(index);
  }
  m_applying_multiple = false;
  m_cursor = states[primary].cursor;
  m_anchor = states[primary].anchor;
  m_secondary.clear();
  // Adjacent selected matches can collapse to the same caret after deletion.
  for (std::size_t i = 0; i < primary; ++i)
    if (states[i].cursor != m_cursor || states[i].anchor != m_anchor)
      if (std::none_of(m_secondary.begin(), m_secondary.end(),
                       [&](const auto& s)
                       {
                         return s.cursor == states[i].cursor &&
                                s.anchor == states[i].anchor;
                       }))
        m_secondary.push_back(states[i]);
  transaction.after = m_cursor;
  transaction.after_anchor = m_anchor;
  transaction.after_secondary = m_secondary;
  transaction.after_state = m_state;
  transaction.parts = std::move(m_history);
  m_history = std::move(history);
  m_history_position = history_position;
  if (!transaction.parts.empty())
  {
    m_history.resize(m_history_position);
    m_history.push_back(std::move(transaction));
    m_history_position = m_history.size();
  }
  m_preferred_column.reset();
  m_match_whole_word = false;
  BreakUndoGroup();
  return true;
}
}  // namespace ptoolkit::editor
