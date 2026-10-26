// Real Windows INPUT_RECORD -> FTXUI parser -> multi-selection editing.
#include "ptoolkit_editor/editor.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

int main()
{
  using namespace ftxui;
  auto document = std::make_shared<ptoolkit::editor::Document>("old old\nold");
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document);
  auto screen = ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);
  screen.ForceHandleCtrlZ(false);
  std::atomic<bool> finished = false;
  bool completed = false;
  auto view = Renderer(
      editor, [editor] { return editor->Render() | borderRounded | flex; });
  view = CatchEvent(view,
                    [&](Event event)
                    {
                      if (event != Event::F10 && event != Event::F9)
                        return false;
                      completed = event == Event::F10;
                      screen.ExitLoopClosure()();
                      return true;
                    });
  std::thread producer(
      [&]
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<INPUT_RECORD> records;
        auto key = [&](WORD vk, wchar_t ch, DWORD modifiers = 0)
        {
          INPUT_RECORD record{};
          record.EventType = KEY_EVENT;
          record.Event.KeyEvent.bKeyDown = TRUE;
          record.Event.KeyEvent.wRepeatCount = 1;
          record.Event.KeyEvent.wVirtualKeyCode = vk;
          record.Event.KeyEvent.uChar.UnicodeChar = ch;
          record.Event.KeyEvent.dwControlKeyState = modifiers;
          records.push_back(record);
        };
        for (int i = 0; i < 3; ++i) key('D', 4, LEFT_CTRL_PRESSED);
        for (wchar_t ch : std::wstring_view(L"new")) key(0, ch);
        key('Z', 26, LEFT_CTRL_PRESSED);
        key('Z', 26, LEFT_CTRL_PRESSED | SHIFT_PRESSED);
        key(VK_F10, 0);
        DWORD written = 0;
        WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), records.data(),
                           static_cast<DWORD>(records.size()), &written);
        for (int i = 0; i < 100 && !finished; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!finished) screen.PostEvent(Event::F9);
      });
  screen.Loop(view);
  finished = true;
  producer.join();
  const bool passed = completed && document->Text() == "new new\nnew" &&
                      document->CursorCount() == 3;
  std::cout
      << (passed
              ? "Native Ctrl+D, multi-caret typing and undo/redo probe passed\n"
              : "Native multi-selection probe FAILED\n");
  return passed ? 0 : 1;
}
