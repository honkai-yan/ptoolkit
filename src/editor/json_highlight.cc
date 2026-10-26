#include "ptoolkit_editor/json_highlight.h"

namespace ptoolkit::editor
{
namespace
{
bool Digit(char ch) { return ch >= '0' && ch <= '9'; }
bool Space(char ch) { return ch == ' ' || ch == '\t'; }
}  // namespace
std::vector<Token> HighlightJsonLine(std::string_view line)
{
  std::vector<Token> tokens;
  std::size_t i = 0;
  while (i < line.size())
  {
    const auto begin = i;
    TokenKind kind = TokenKind::Text;
    if (Space(line[i]))
    {
      while (i < line.size() && Space(line[i])) ++i;
    }
    else if (line[i] == '"')
    {
      ++i;
      bool closed = false;
      while (i < line.size())
      {
        if (line[i] == '\\')
        {
          i += (i + 1 < line.size() ? 2 : 1);
          continue;
        }
        if (line[i++] == '"')
        {
          closed = true;
          break;
        }
      }
      auto after = i;
      while (after < line.size() && Space(line[after])) ++after;
      kind = closed && after < line.size() && line[after] == ':'
                 ? TokenKind::Property
                 : TokenKind::String;
    }
    else if (Digit(line[i]) || line[i] == '-')
    {
      if (line[i] == '-') ++i;
      while (i < line.size() && Digit(line[i])) ++i;
      if (i < line.size() && line[i] == '.')
      {
        ++i;
        while (i < line.size() && Digit(line[i])) ++i;
      }
      if (i < line.size() && (line[i] == 'e' || line[i] == 'E'))
      {
        ++i;
        if (i < line.size() && (line[i] == '+' || line[i] == '-')) ++i;
        while (i < line.size() && Digit(line[i])) ++i;
      }
      kind = TokenKind::Number;
    }
    else if (std::string_view("{}[]:,").find(line[i]) != std::string_view::npos)
    {
      ++i;
      kind = TokenKind::Punctuation;
    }
    else
    {
      while (i < line.size() && !Space(line[i]) &&
             std::string_view("{}[]:,\"").find(line[i]) ==
                 std::string_view::npos)
        ++i;
      if (i == begin) ++i;
      const auto value = line.substr(begin, i - begin);
      kind = value == "true" || value == "false" || value == "null"
                 ? TokenKind::Keyword
                 : TokenKind::Invalid;
    }
    tokens.push_back({begin, i, kind});
  }
  return tokens;
}

ftxui::Color JsonTokenColor(TokenKind kind)
{
  // VS Code's built-in Dark+/Dark VS token palette, not semantic analysis.
  using ftxui::Color;
  switch (kind)
  {
    case TokenKind::Property:
      return Color::RGB(0x9c, 0xdc, 0xfe);
    case TokenKind::String:
      return Color::RGB(0xce, 0x91, 0x78);
    case TokenKind::Number:
      return Color::RGB(0xb5, 0xce, 0xa8);
    case TokenKind::Keyword:
      return Color::RGB(0x56, 0x9c, 0xd6);
    case TokenKind::Invalid:
      return Color::RGB(0xf4, 0x47, 0x47);
    default:
      return Color::RGB(0xd4, 0xd4, 0xd4);
  }
}
}  // namespace ptoolkit::editor
