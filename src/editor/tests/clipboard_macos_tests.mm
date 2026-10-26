#include "../clipboard_macos.h"
#import <AppKit/AppKit.h>
#include <cstdlib>
#include <iostream>

void Check(bool ok, const char* message)
{
  if (!ok) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}
int main()
{
  @autoreleasepool
  {
    NSString* name = [[NSUUID UUID] UUIDString];
    NSPasteboard* board = [NSPasteboard pasteboardWithName:name];
    auto clipboard = ptoolkit::editor::detail::MakeMacClipboard(name.UTF8String);
    [board clearContents];
    Check(!clipboard->Read(), "empty pasteboard has no text");
    const std::string text = "copy 你好 😀\nline 2\tend\r\n";
    Check(clipboard->Write(text).has_value(), "write UTF-8 text");
    Check(clipboard->Read() == std::expected<std::string, std::string>(text), "Unicode roundtrip");
    Check(!clipboard->Write(std::string("bad\0text", 8)), "reject embedded NUL");
    Check(!clipboard->Write("\xff"), "reject invalid UTF-8");
    Check(clipboard->Read() == std::expected<std::string, std::string>(text), "invalid write preserves text");
    Check(clipboard->Write("").has_value(), "write empty text");
    Check(clipboard->Read() == std::expected<std::string, std::string>(""), "empty text roundtrip");
    [board releaseGlobally];
    std::cout << "Isolated macOS clipboard tests passed\n";
  }
}
