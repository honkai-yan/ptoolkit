#include "ptoolkit_editor/clipboard.h"

#include <algorithm>
#include <cstring>
#include <limits>

#ifdef __APPLE__
#include "clipboard_macos.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace ptoolkit::editor
{
namespace
{
#ifdef _WIN32
std::string Error(const char* operation)
{
  return std::string(operation) + " failed (Win32 " +
         std::to_string(GetLastError()) + ")";
}
struct ClipboardGuard
{
  ~ClipboardGuard() { CloseClipboard(); }
};
struct MemoryGuard
{
  HGLOBAL memory;
  ~MemoryGuard()
  {
    if (memory) GlobalFree(memory);
  }
};
struct LockGuard
{
  HGLOBAL memory;
  ~LockGuard() { GlobalUnlock(memory); }
};

class SystemClipboard final : public Clipboard
{
public:
  ~SystemClipboard() override
  {
    if (m_owner) DestroyWindow(m_owner);
  }
  std::expected<void, std::string> Write(std::string_view text) override
  {
    // CF_UNICODETEXT convention uses CRLF. Reject embedded NULs rather than
    // silently truncate a copied selection.
    if (text.find('\0') != std::string_view::npos)
      return std::unexpected("Clipboard text contains a NUL character");
    std::string windows_text;
    windows_text.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
      if (text[i] == '\n' && (!i || text[i - 1] != '\r')) windows_text += '\r';
      windows_text += text[i];
    }
    if (windows_text.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return std::unexpected("Clipboard text is too large");
    const int count =
        windows_text.empty()
            ? 0
            : MultiByteToWideChar(
                  CP_UTF8, MB_ERR_INVALID_CHARS, windows_text.data(),
                  static_cast<int>(windows_text.size()), nullptr, 0);
    if (!windows_text.empty() && !count)
      return std::unexpected(Error("UTF-8 conversion"));
    MemoryGuard memory{
        GlobalAlloc(GMEM_MOVEABLE,
                    (static_cast<std::size_t>(count) + 1) * sizeof(wchar_t))};
    if (!memory.memory) return std::unexpected(Error("Clipboard allocation"));
    auto* buffer = static_cast<wchar_t*>(GlobalLock(memory.memory));
    if (!buffer) return std::unexpected(Error("Clipboard lock"));
    {
      LockGuard lock{memory.memory};
      if (count && !MultiByteToWideChar(
                       CP_UTF8, MB_ERR_INVALID_CHARS, windows_text.data(),
                       static_cast<int>(windows_text.size()), buffer, count))
        return std::unexpected(Error("UTF-8 conversion"));
      buffer[count] = L'\0';
    }
    // A real owner is required: OpenClipboard(nullptr) + EmptyClipboard()
    // cannot be followed by SetClipboardData(). Create it lazily on the UI
    // thread.
    if (!m_owner)
      m_owner = CreateWindowExW(0, L"STATIC", L"ptoolkit editor clipboard", 0,
                                0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!m_owner) return std::unexpected(Error("Clipboard owner creation"));
    if (!OpenClipboard(m_owner)) return std::unexpected(Error("OpenClipboard"));
    ClipboardGuard clipboard;
    if (!EmptyClipboard()) return std::unexpected(Error("EmptyClipboard"));
    if (!SetClipboardData(CF_UNICODETEXT, memory.memory))
      return std::unexpected(Error("SetClipboardData"));
    memory.memory = nullptr;  // Ownership transferred to Windows.
    return {};
  }

  std::expected<std::string, std::string> Read() override
  {
    if (!OpenClipboard(m_owner)) return std::unexpected(Error("OpenClipboard"));
    ClipboardGuard clipboard;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
      return std::unexpected("Clipboard contains no text");
    auto memory = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
    if (!memory) return std::unexpected(Error("GetClipboardData"));
    const auto capacity = GlobalSize(memory) / sizeof(wchar_t);
    const auto* buffer = static_cast<const wchar_t*>(GlobalLock(memory));
    if (!buffer) return std::unexpected(Error("Clipboard lock"));
    LockGuard lock{memory};
    const auto* end = std::find(buffer, buffer + capacity, L'\0');
    if (end == buffer + capacity)
      return std::unexpected("Clipboard text is not terminated");
    if (end - buffer > std::numeric_limits<int>::max())
      return std::unexpected("Clipboard text is too large");
    const int length = static_cast<int>(end - buffer);
    if (!length) return std::string{};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer,
                                          length, nullptr, 0, nullptr, nullptr);
    if (!count) return std::unexpected(Error("Clipboard UTF-8 conversion"));
    std::string text(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer, length,
                             text.data(), count, nullptr, nullptr))
      return std::unexpected(Error("Clipboard UTF-8 conversion"));
    return text;
  }

private:
  HWND m_owner = nullptr;
};
#else
class SystemClipboard final : public Clipboard
{
public:
  std::expected<void, std::string> Write(std::string_view) override
  {
    return std::unexpected("Provide a Clipboard backend for this platform");
  }
  std::expected<std::string, std::string> Read() override
  {
    return std::unexpected("Provide a Clipboard backend for this platform");
  }
};
#endif
}  // namespace
std::shared_ptr<Clipboard> MakeSystemClipboard()
{
#ifdef __APPLE__
  return detail::MakeMacClipboard();
#else
  return std::make_shared<SystemClipboard>();
#endif
}
}  // namespace ptoolkit::editor
