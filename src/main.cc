#include "app.h"
#include "config.h"
#include "ui/ui.h"

#include <cstdio>
#include <print>

#include <ftxui/component/screen_interactive.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

int main()
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif

  std::println("{} {}", ptoolkit::kAppName, ptoolkit::GetAppVersion());
  std::println("Initing...");
  std::fflush(stdout);

  auto& app = ptoolkit::App::GetInstance();
  if (const auto result = app.Init(); !result)
  {
    std::println(stderr, "Fatal error: {}", result.error());
    return 1;
  }

  auto screen = ftxui::ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);
  screen.ForceHandleCtrlZ(false);
  auto view = ptoolkit::CreateApplicationView(app, screen.ExitLoopClosure());

  // Loop enters the alternate screen and restores the original screen on exit.
  screen.Loop(view);
  return 0;
}
