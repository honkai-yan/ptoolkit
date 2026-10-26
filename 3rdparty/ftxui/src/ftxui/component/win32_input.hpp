// Local ptoolkit adapter for FTXUI 7.0.3. See 3rdparty/README.md.
#pragma once

#include <string>
#include <windows.h>

namespace ftxui::detail {
inline std::string Win32SpecialKey(const KEY_EVENT_RECORD& key) {
  const bool shift = key.dwControlKeyState & SHIFT_PRESSED;
  const bool control = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
  const bool alt = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);
  const int modifier = 1 + shift + 2 * alt + 4 * control;
  if (key.wVirtualKeyCode == 'Z' && control && shift) return "\x1b[122;6u";
  if (key.wVirtualKeyCode == VK_TAB && shift) return "\x1b[Z";
  char suffix = 0;
  switch (key.wVirtualKeyCode) {
    case VK_UP: suffix = 'A'; break;
    case VK_DOWN: suffix = 'B'; break;
    case VK_RIGHT: suffix = 'C'; break;
    case VK_LEFT: suffix = 'D'; break;
    case VK_HOME: suffix = 'H'; break;
    case VK_END: suffix = 'F'; break;
  }
  if (suffix) return modifier == 1 ? std::string("\x1b[") + suffix
                                  : "\x1b[1;" + std::to_string(modifier) + suffix;
  if (key.wVirtualKeyCode >= VK_F1 && key.wVirtualKeyCode <= VK_F4) {
    const char end = 'P' + key.wVirtualKeyCode - VK_F1;
    return modifier == 1 ? std::string("\x1bO") + end
                         : "\x1b[1;" + std::to_string(modifier) + end;
  }
  int code = 0;
  switch (key.wVirtualKeyCode) {
    case VK_INSERT: code = 2; break;
    case VK_DELETE: code = 3; break;
    case VK_PRIOR: code = 5; break;
    case VK_NEXT: code = 6; break;
    case VK_F5: code = 15; break;
    case VK_F6: code = 17; break;
    case VK_F7: code = 18; break;
    case VK_F8: code = 19; break;
    case VK_F9: code = 20; break;
    case VK_F10: code = 21; break;
    case VK_F11: code = 23; break;
    case VK_F12: code = 24; break;
  }
  if (code) return "\x1b[" + std::to_string(code) +
                   (modifier == 1 ? "" : ";" + std::to_string(modifier)) + "~";
  return {};
}
}  // namespace ftxui::detail
