#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <ftxui/screen/color.hpp>

namespace ptoolkit::editor
{
enum class TokenKind
{
  Text,
  Property,
  String,
  Number,
  Keyword,
  Punctuation,
  Invalid
};
struct Token
{
  std::size_t begin, end;
  TokenKind kind;
};
std::vector<Token> HighlightJsonLine(std::string_view line);
ftxui::Color JsonTokenColor(TokenKind kind);
}  // namespace ptoolkit::editor
