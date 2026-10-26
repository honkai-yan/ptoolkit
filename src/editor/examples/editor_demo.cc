#include "ptoolkit_editor/editor.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
class ProbeClipboard : public ptoolkit::editor::Clipboard
{
public:
  std::string text;
  std::expected<void, std::string> Write(std::string_view value) override
  {
    text = value;
    return {};
  }
  std::expected<std::string, std::string> Read() override { return text; }
};
}  // namespace

int main(int argc, char** argv)
{
  using namespace ftxui;
  const bool terminal_probe =
      argc > 1 && std::string_view(argv[1]) == "--terminal-probe";
  const bool probe = terminal_probe ||
      (argc > 1 && std::string_view(argv[1]) == "--keyboard-probe");
  const std::string original =
      "{\n  \"name\": \"Standalone FTXUI editor\",\n  \"enabled\": true,\n  "
      "\"number\": 42,\n  \"empty\": null\n}\n";
  auto document = std::make_shared<ptoolkit::editor::Document>(original);
  auto clipboard = probe ? std::make_shared<ProbeClipboard>() : nullptr;
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document, clipboard);
  auto screen = ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);
  screen.ForceHandleCtrlZ(false);
  std::atomic<bool> finished = false;
  bool undo = false, redo = false;
  bool copied = false, pasted = false, selected = false;
  bool home = false, end = false, left = false, right = false,
       focus_key = false;
  unsigned long written_records = 0, write_error = 0;
  auto view =
      Renderer(editor,
               [editor]
               {
                 return vbox({text("JSON editor | F10: exit | Ctrl+X/C/V: "
                                   "cut/copy/paste | Shift+arrows: select") |
                                  hcenter,
                              separator(), editor->Render() | flex}) |
                        borderRounded | flex;
               });
  view = CatchEvent(view,
                    [&](Event event)
                    {
                      if (event == Event::F10)
                      {
                        screen.ExitLoopClosure()();
                        return true;
                      }
                      if (!probe) return false;
                      if (event == Event::CtrlC) copied = true;
                      if (event == Event::CtrlV) pasted = true;
                      if (event == Event::Special("\x1b[1;2H")) selected = true;
                      if (event == Event::F6)
                      {
                        focus_key = true;
                        finished = true;
                        screen.ExitLoopClosure()();
                        return true;
                      }
                      if (event == Event::Special("\x1b[1;5H")) home = true;
                      if (event == Event::Special("\x1b[1;5F")) end = true;
                      if (event == Event::ArrowLeft) left = true;
                      if (event == Event::ArrowRight) right = true;
                      if (event == Event::CtrlZ) undo = true;
                      if (event == Event::Special("\x1b[122;6u")) redo = true;
                      const bool handled = editor->OnEvent(event);
                      return handled;
                    });

  std::thread native_input;
  if (probe && !terminal_probe)
  {
#ifdef _WIN32
    native_input = std::thread(
        [&]
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          INPUT_RECORD records[12] = {};
          for (auto& record : records)
          {
            record.EventType = KEY_EVENT;
            record.Event.KeyEvent.bKeyDown = TRUE;
            record.Event.KeyEvent.wRepeatCount = 1;
          }
          records[0].Event.KeyEvent.uChar.UnicodeChar = L'x';
          for (int i = 1; i < 3; ++i)
          {
            records[i].Event.KeyEvent.wVirtualKeyCode = 'Z';
            records[i].Event.KeyEvent.uChar.UnicodeChar = 0x1a;
            records[i].Event.KeyEvent.dwControlKeyState =
                LEFT_CTRL_PRESSED | (i == 2 ? SHIFT_PRESSED : 0);
          }
          records[3].Event.KeyEvent.wVirtualKeyCode = VK_HOME;
          records[3].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
          records[4].Event.KeyEvent.wVirtualKeyCode = VK_RIGHT;
          records[5].Event.KeyEvent.wVirtualKeyCode = VK_END;
          records[5].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
          records[6].Event.KeyEvent.wVirtualKeyCode = VK_LEFT;
          records[7].Event.KeyEvent.wVirtualKeyCode = VK_HOME;
          records[7].Event.KeyEvent.dwControlKeyState = SHIFT_PRESSED;
          records[8].Event.KeyEvent.wVirtualKeyCode = 'C';
          records[8].Event.KeyEvent.uChar.UnicodeChar = 3;
          records[8].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
          records[9].Event.KeyEvent.wVirtualKeyCode = VK_END;
          records[9].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
          records[10].Event.KeyEvent.wVirtualKeyCode = 'V';
          records[10].Event.KeyEvent.uChar.UnicodeChar = 22;
          records[10].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
          records[11].Event.KeyEvent.wVirtualKeyCode = VK_F6;
          DWORD written = 0;
          if (!WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), records, 12,
                                  &written))
            write_error = GetLastError();
          written_records = written;
          for (int i = 0; i < 100 && !finished; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          if (!finished) screen.PostEvent(Event::F10);
        });
#else
    std::cerr << "--keyboard-probe requires a Windows console\n";
    return 2;
#endif
  }
  screen.Loop(view);
  finished = true;
  if (native_input.joinable()) native_input.join();
  if (probe)
  {
    const bool passed = undo && redo && home && end && left && right &&
                        focus_key && copied && pasted && selected &&
                        clipboard->text == "}" &&
                        document->Text() == "x" + original + "}" &&
                        document->Cursor() == ptoolkit::editor::Position{6, 1};
    std::cout << "records=" << written_records << " error=" << write_error
              << " undo=" << undo << " redo=" << redo << '\n';
    std::cout << (passed ? "Native selection, Ctrl+C/V, undo/redo, navigation "
                           "probe passed\n"
                         : "Native keyboard probe FAILED\n");
    return passed ? 0 : 1;
  }
}
