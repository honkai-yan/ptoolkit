#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace ptoolkit::editor
{
// A host can provide another platform backend without changing the editor.
// Text crosses this boundary as UTF-8. Errors never modify the document.
class Clipboard
{
public:
  virtual ~Clipboard() = default;
  virtual std::expected<void, std::string> Write(std::string_view text) = 0;
  virtual std::expected<std::string, std::string> Read() = 0;
};
std::shared_ptr<Clipboard> MakeSystemClipboard();
}  // namespace ptoolkit::editor
