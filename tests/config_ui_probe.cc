// Run in a real Windows console. All files are isolated fixtures in the build
// directory; this never reads/writes the user's settings or presets.
#include "app.h"
#include "config_storage.h"
#include "ui/ui.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

int main()
{
  using namespace ftxui;
  const auto root =
      std::filesystem::current_path() /
      ("native-config-fixture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup
  {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{root};
  const auto settings = root / "settings.json";
  const auto presets = root / "presets";
  const std::string original = "{\"model\":\"old\"}\n";
  if (!ptoolkit::WriteConfigText(settings, original)) return 1;
  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("claude_code_config_man", settings);
  auto screen = ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);
  screen.ForceHandleCtrlZ(false);
  std::atomic<bool> finished = false;
  bool completed = false;
  auto view = ptoolkit::CreateApplicationView(app, screen.ExitLoopClosure(),
                                              settings, presets);
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
        key(VK_F6, 0);
        key(VK_SPACE, L' ');
        key('S', 19, LEFT_CTRL_PRESSED);
        key('P', 16, LEFT_CTRL_PRESSED);
        key(VK_RETURN, L'\r');
        for (wchar_t ch : std::wstring_view(L"demo")) key(0, ch);
        key(VK_RETURN, L'\r');
        key('R', L'r');
        for (int i = 0; i < 4; ++i) key(VK_BACK, 8);
        for (wchar_t ch : std::wstring_view(L"renamed")) key(0, ch);
        key(VK_RETURN, L'\r');
        key('D', L'd');
        key(VK_ESCAPE, 27);
        key('P', 16, LEFT_CTRL_PRESSED);
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
  const auto expected = " " + original;
  const bool passed =
      completed && ptoolkit::ReadConfigText(settings) == expected &&
      ptoolkit::ReadConfigText(presets / "renamed.json") == expected &&
      !std::filesystem::exists(presets / "demo.json");
  std::cout << (passed ? "Native Ctrl+S/P, preset prompt/rename and delete "
                         "cancellation probe passed\n"
                       : "Native configuration UI probe FAILED\n");
  app.ExecuteCommand("home");
  return passed ? 0 : 1;
}
