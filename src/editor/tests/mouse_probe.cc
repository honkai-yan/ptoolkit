// Run in a real Windows console: native INPUT_RECORD -> FTXUI -> Editor.
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
  using ptoolkit::editor::Position;
  std::string content = "zero\nalpha\nbeta\ngamma\ndelta\n";
  for (int i = 5; i < 100; ++i) content += "row" + std::to_string(i) + '\n';
  auto document = std::make_shared<ptoolkit::editor::Document>(content);
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document);
  auto screen = ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);
  screen.ForceHandleCtrlZ(false);
  int phase = 0, failures = 0;
  std::atomic<bool> finished = false;
  std::atomic<int> acknowledged = 0;
  std::vector<std::string> diagnostics;
  auto view = Renderer(
      editor,
      [editor]
      {
        // Matches the host's top border, title, divider, filename and pinned
        // input.
        return vbox({text("Native mouse coordinate regression"), separator(),
                     text("settings.json"), editor->Render() | flex,
                     text("status"), text("command input") | borderEmpty}) |
               borderRounded | flex;
      });
  view = CatchEvent(
      view,
      [&](Event event)
      {
        if (event == Event::F10 || event == Event::CtrlC)
        {
          screen.ExitLoopClosure()();
          return true;
        }
        if (event != Event::F9) return false;
        const auto cursor = document->Cursor();
        bool correct = false;
        switch (phase)
        {
          case 0:
            correct = cursor == Position{2, 2};
            break;
          case 1:
            correct = cursor == Position{0, 0};
            break;
          case 2:
            correct = document->SelectedText() == "lpha\nbeta\nga";
            break;
          case 3:
            correct = cursor == Position{5, 2};
            break;
          case 4:
            correct = document->SelectedText() == "delta";
            break;
          case 5:
            correct = document->SelectedText() == "delta\n";
            break;
        }
        if (!correct)
        {
          ++failures;
          diagnostics.push_back("phase " + std::to_string(phase) +
                                ": cursor=" + std::to_string(cursor.line) +
                                "," + std::to_string(cursor.column));
        }
        acknowledged = ++phase;
        if (phase == 6)
        {
          finished = true;
          screen.ExitLoopClosure()();
        }
        return true;
      });
  std::thread producer(
      [&]
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CONSOLE_SCREEN_BUFFER_INFO info{};
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
        std::vector<INPUT_RECORD> records;
        // 101 lines => 3 gutter digits + 2 padding; editor begins at (1,4).
        auto mouse = [&](int line, int column, DWORD buttons, DWORD flags = 0)
        {
          INPUT_RECORD record{};
          record.EventType = MOUSE_EVENT;
          record.Event.MouseEvent.dwMousePosition = {
              static_cast<SHORT>(6 + column + info.srWindow.Left),
              static_cast<SHORT>(4 + line + info.srWindow.Top)};
          record.Event.MouseEvent.dwButtonState = buttons;
          record.Event.MouseEvent.dwEventFlags = flags;
          records.push_back(record);
        };
        auto check = [&]
        {
          INPUT_RECORD record{};
          record.EventType = KEY_EVENT;
          record.Event.KeyEvent.bKeyDown = TRUE;
          record.Event.KeyEvent.wRepeatCount = 1;
          record.Event.KeyEvent.wVirtualKeyCode = VK_F9;
          records.push_back(record);
        };
        auto click = [&](int line, int column)
        {
          mouse(line, column, FROM_LEFT_1ST_BUTTON_PRESSED);
          mouse(line, column, 0);
          check();
        };
        auto submit = [&](int stage)
        {
          DWORD written = 0;
          WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), records.data(),
                             static_cast<DWORD>(records.size()), &written);
          records.clear();
          for (int i = 0; i < 100 && acknowledged < stage && !finished; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        click(2, 2);
        submit(1);
        click(0, 0);
        submit(2);
        mouse(1, 1, FROM_LEFT_1ST_BUTTON_PRESSED);
        mouse(3, 2, FROM_LEFT_1ST_BUTTON_PRESSED, MOUSE_MOVED);
        mouse(3, 2, 0);
        check();
        submit(3);
        mouse(2, 2, static_cast<DWORD>(static_cast<WORD>(-WHEEL_DELTA)) << 16,
              MOUSE_WHEELED);
        click(2, 2);
        submit(4);
        mouse(1, 2, FROM_LEFT_1ST_BUTTON_PRESSED);
        mouse(1, 2, 0);
        mouse(1, 2, FROM_LEFT_1ST_BUTTON_PRESSED, DOUBLE_CLICK);
        mouse(1, 2, 0);
        check();
        submit(5);
        click(1, 2);
        submit(6);
        for (int i = 0; i < 100 && !finished; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!finished) screen.PostEvent(Event::F10);
      });
  screen.Loop(view);
  finished = true;
  producer.join();
  for (const auto& diagnostic : diagnostics) std::cout << diagnostic << '\n';
  const bool passed = phase == 6 && !failures;
  std::cout << (passed ? "Native mouse click/first-row/drag/scrolled-click "
                         "and double/triple-click probe passed\n"
                       : "Native mouse coordinate probe FAILED\n");
  return passed ? 0 : 1;
}
