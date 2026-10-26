#include "clipboard_macos.h"

#import <AppKit/AppKit.h>

namespace ptoolkit::editor::detail
{
namespace
{
class MacClipboard final : public Clipboard
{
public:
  explicit MacClipboard(NSPasteboard* board) : m_board(board) {}

  std::expected<void, std::string> Write(std::string_view text) override
  {
    @autoreleasepool
    {
      if (!m_board) return std::unexpected("Cannot connect to macOS pasteboard");
      if (text.find('\0') != std::string_view::npos)
        return std::unexpected("Clipboard text contains a NUL character");
      NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
      if (!value) return std::unexpected("Clipboard text is not valid UTF-8");
      [m_board clearContents];
      if (![m_board setString:value forType:NSPasteboardTypeString])
        return std::unexpected("Cannot write macOS pasteboard text");
      return {};
    }
  }

  std::expected<std::string, std::string> Read() override
  {
    @autoreleasepool
    {
      if (!m_board) return std::unexpected("Cannot connect to macOS pasteboard");
      NSString* value = [m_board stringForType:NSPasteboardTypeString];
      if (!value) return std::unexpected("Clipboard contains no text");
      NSData* data = [value dataUsingEncoding:NSUTF8StringEncoding];
      if (!data) return std::unexpected("Cannot convert macOS pasteboard text to UTF-8");
      if (!data.length) return std::string{};
      return std::string(static_cast<const char*>(data.bytes), data.length);
    }
  }

private:
  NSPasteboard* m_board;
};
}

std::shared_ptr<Clipboard> MakeMacClipboard(std::string_view name)
{
  @autoreleasepool
  {
    NSPasteboard* board = nil;
    if (name.empty())
      board = [NSPasteboard generalPasteboard];
    else
    {
      NSString* value = [[NSString alloc] initWithBytes:name.data() length:name.size()
                                              encoding:NSUTF8StringEncoding];
      if (value) board = [NSPasteboard pasteboardWithName:value];
    }
    return std::make_shared<MacClipboard>(board);
  }
}
}
