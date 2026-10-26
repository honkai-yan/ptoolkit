#include "app.h"
#include "claude_config.h"
#include "ui/command_input.h"
#include "config_storage.h"
#include "ui/ui.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{
using namespace ftxui;
using ptoolkit::CommandInput;

void Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Screen Draw(Component component, int width = 80, int height = 24)
{
  Screen screen(width, height);
  Render(screen, component->Render());
  return screen;
}

Screen DrawEditor(const std::shared_ptr<CommandInput>& input, int width = 12)
{
  Screen screen(width, 30);
  Render(screen, vbox({filler(), input->Render()}));
  return screen;
}

std::string Line(Screen& screen, int y)
{
  std::string result;
  for (int x = 0; x < screen.dimx(); ++x)
    result += screen.CellAt(x, y).character;
  return result;
}

void CompletionAndPages()
{
  auto& app = ptoolkit::App::GetInstance();
  Check(app.ExecuteCommand("home"), "home command exists");
  int exits = 0;
  auto view = ptoolkit::CreateApplicationView(app, [&] { ++exits; });
  auto screen = Draw(view);
  Check(Line(screen, 1).find("ptoolkit v0.1.0") != std::string::npos,
        "persistent title");
  Check(Line(screen, 2).find("─") != std::string::npos,
        "persistent title separator");
  Check(Line(screen, 4).find("System") != std::string::npos, "home table");
  const auto system_line = Line(screen, 4);
  const auto input_background = Color::RGB(0x29, 0x29, 0x29);
  const auto blue = Color::RGB(0x63, 0xa8, 0xf8);
  Check(screen.CellAt(1, 20).background_color == input_background &&
            screen.CellAt(1, 22).background_color == input_background,
        "three-row input pinned above bottom border");
  Check(screen.CellAt(1, 19).background_color != input_background,
        "initial input has exactly three rows");

  view->OnEvent(Event::Character('h'));
  screen = Draw(view);
  Check(Line(screen, 18).find("home") != std::string::npos &&
            Line(screen, 19).find("hello") != std::string::npos,
        "prefix opens two suggestions above input");
  Check(Line(screen, 4) == system_line, "popup does not shift page content");
  Check(screen.CellAt(2, 18).background_color == blue,
        "first suggestion selected");
  view->OnEvent(Event::ArrowDown);
  screen = Draw(view);
  Check(screen.CellAt(2, 19).background_color == blue, "down selects hello");
  view->OnEvent(Event::ArrowUp);
  screen = Draw(view);
  Check(screen.CellAt(2, 18).background_color == blue, "up selects home");
  view->OnEvent(Event::Character('e'));
  screen = Draw(view);
  Check(Line(screen, 19).find("hello") != std::string::npos &&
            Line(screen, 18).find("home") == std::string::npos,
        "editing an open menu filters immediately");
  view->OnEvent(Event::Character('x'));
  screen = Draw(view);
  Check(Line(screen, 19).find("hello") == std::string::npos,
        "nonmatching input hides menu");
  view->OnEvent(Event::Backspace);
  view->OnEvent(Event::Tab);
  screen = Draw(view);
  Check(Line(screen, 21).find("hello") != std::string::npos,
        "tab completes without submitting");
  Check(app.GetPage() == ptoolkit::App::Page::Home, "tab preserves page");
  view->OnEvent(Event::Return);
  screen = Draw(view);
  Check(app.GetPage() == ptoolkit::App::Page::Hello &&
            Line(screen, 3).find("hello world") != std::string::npos,
        "enter submits hello");
  Check(Line(screen, 21).find("hello") == std::string::npos,
        "submit clears input");
  Check(Line(screen, 1).find("ptoolkit v0.1.0") != std::string::npos,
        "page change preserves title");
  view->OnEvent(Event::Character("home"));
  view->OnEvent(Event::Return);
  Check(app.GetPage() == ptoolkit::App::Page::Home,
        "home returns to hardware page");

  view->OnEvent(Event::Character('q'));
  view->OnEvent(Event::Escape);
  Check(exits == 0, "q and escape never exit");
  screen = Draw(view);
  Check(Line(screen, 21).find('q') != std::string::npos, "q is ordinary input");
  view->OnEvent(Event::CtrlC);
  Check(exits == 1, "only Ctrl+C invokes exit callback");
}

void WrappingAndEditing()
{
  auto input =
      std::make_shared<CommandInput>(std::vector<ptoolkit::Command>{},
                                     [](const std::string&) { return false; });
  DrawEditor(input);
  input->OnEvent(Event::Character("abcdefghijklmnopqrstuvwxy"));
  auto screen = DrawEditor(input);
  Check(Line(screen, 26).find("abcdefghij") != std::string::npos &&
            Line(screen, 27).find("klmnopqrst") != std::string::npos &&
            Line(screen, 28).find("uvwxy") != std::string::npos,
        "soft wrapping grows content to three rows");
  input->OnEvent(Event::ArrowUp);
  Check(input->Cursor() == 15, "up retains visual column on wrapped line");
  input->OnEvent(Event::ArrowUp);
  Check(input->Cursor() == 5, "up retains preferred column across rows");
  input->OnEvent(Event::ArrowDown);
  Check(input->Cursor() == 15, "down moves cursor in wrapped input");
  input->OnEvent(Event::ArrowLeft);
  input->OnEvent(Event::Character('!'));
  Check(input->Content() == "abcdefghijklmn!opqrstuvwxy",
        "insertion at cursor");
  input->OnEvent(Event::Backspace);
  input->OnEvent(Event::Delete);
  Check(input->Content() == "abcdefghijklmnpqrstuvwxy", "backspace and delete");

  auto long_input =
      std::make_shared<CommandInput>(std::vector<ptoolkit::Command>{},
                                     [](const std::string&) { return false; });
  DrawEditor(long_input);
  long_input->OnEvent(Event::Character(std::string(120, 'x') + "END"));
  screen = DrawEditor(long_input);
  const auto input_background = Color::RGB(0x29, 0x29, 0x29);
  Check(screen.CellAt(0, 20).background_color == input_background &&
            screen.CellAt(0, 19).background_color != input_background,
        "height capped at eight content rows plus two padding rows");
  Check(Line(screen, 28).find("END") != std::string::npos,
        "overflow scrolls to the caret");
  Mouse wheel;
  wheel.button = Mouse::WheelUp;
  wheel.x = 2;
  wheel.y = 25;
  for (int i = 0; i < 20; ++i) long_input->OnEvent(Event::Mouse("", wheel));
  screen = DrawEditor(long_input);
  Check(Line(screen, 28).find("END") == std::string::npos &&
            screen.cursor().shape == Screen::Cursor::Hidden,
        "mouse wheel can browse input without moving the caret");
  Check(long_input->Cursor() == long_input->Content().size(),
        "scrolling preserves insertion position");
  long_input->OnEvent(Event::CtrlA);
  screen = DrawEditor(long_input);
  Check(Line(screen, 21).find("xxxxxxxxxx") != std::string::npos &&
            screen.cursor().y == 21,
        "moving to start scrolls to first row");
  long_input->OnEvent(Event::CtrlE);
  screen = DrawEditor(long_input);
  Check(screen.cursor().y == 28, "moving to end scrolls back down");
  screen = DrawEditor(long_input, 50);
  Check(screen.CellAt(0, 25).background_color == input_background &&
            screen.CellAt(0, 24).background_color != input_background,
        "terminal resize recomputes wrapping and editor height in same frame");

  auto unicode =
      std::make_shared<CommandInput>(std::vector<ptoolkit::Command>{},
                                     [](const std::string&) { return false; });
  DrawEditor(unicode, 6);
  const std::string ni = "\xe4\xbd\xa0";
  const std::string hao = "\xe5\xa5\xbd";
  unicode->OnEvent(Event::Character(ni + "a" + hao));
  screen = DrawEditor(unicode, 6);
  Check(Line(screen, 27).find(ni + "a") != std::string::npos &&
            Line(screen, 28).find(hao) != std::string::npos,
        "wide glyphs wrap without splitting UTF-8");
  unicode->OnEvent(Event::ArrowLeft);
  unicode->OnEvent(Event::Backspace);
  Check(unicode->Content() == ni + hao, "unicode-aware cursor and deletion");
  unicode->OnEvent(Event::Character("\r\n"));
  Check(unicode->Content().find_first_of("\r\n") == std::string::npos,
        "pasted newlines do not create manual line breaks");
}

void IndependentPageScrolling()
{
  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("home");
  auto view = ptoolkit::CreateApplicationView(app, [] {});
  auto screen = Draw(view, 80, 14);
  const auto title = Line(screen, 1);
  const auto input_top = Line(screen, 10);
  const auto content_top = Line(screen, 3);
  Mouse wheel;
  wheel.button = Mouse::WheelDown;
  wheel.x = 5;
  wheel.y = 5;
  for (int i = 0; i < 10; ++i) view->OnEvent(Event::Mouse("", wheel));
  screen = Draw(view, 80, 14);
  Check(Line(screen, 3) != content_top, "main content scrolls independently");
  Check(Line(screen, 1) == title, "scroll preserves title");
  Check(Line(screen, 2).find("─") != std::string::npos,
        "scroll preserves title divider");
  Check(Line(screen, 10) == input_top, "scroll preserves input anchor");
  Check(Line(screen, 8).find("Hard disk") != std::string::npos,
        "scroll reveals bottom hardware rows in a short terminal");
  view->OnEvent(Event::Character("hello"));
  view->OnEvent(Event::Return);
  screen = Draw(view, 80, 14);
  Check(Line(screen, 3).find("hello world") != std::string::npos,
        "changing page resets scroll offset");
}

void ClaudeConfiguration()
{
  const auto path =
      std::filesystem::current_path() /
      ("claude-settings-fixture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  const std::string original =
      "{\n  \"env\": {\"DEMO\": \"value\"},\n  \"enabled\": true\n}\n";
  auto write = [&](const std::string& content)
  {
    std::ofstream file(path, std::ios::binary);
    file << content;
  };
  write(original);
  auto loaded = ptoolkit::LoadClaudeConfig(path);
  Check(loaded && loaded->document->Text() == original,
        "JSON load retains formatting and key order");
  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("home");
  int exits = 0;
  auto view = ptoolkit::CreateApplicationView(app, [&] { ++exits; }, path);
  Draw(view);
  view->OnEvent(Event::Character("cla"));
  auto screen = Draw(view);
  Check(Line(screen, 18).find("claude_code_config_man") != std::string::npos &&
            Line(screen, 19).find("claude_code_prompt_man") != std::string::npos,
        "Claude command prefix completion lists both commands");
  view->OnEvent(Event::Tab);
  view->OnEvent(Event::Return);
  screen = Draw(view);
  auto document = app.GetClaudeDocument();
  Check(app.GetPage() == ptoolkit::App::Page::ClaudeCodeConfig && document,
        "command opens parsed configuration");
  Check(Line(screen, 5).find("env") != std::string::npos,
        "editor renders config in main content area");
  Check(screen.cursor().y >= 4 && screen.cursor().y < 19,
        "editor receives focus; command input does not overwrite caret");
  view->OnEvent(Event::Character('x'));
  Check(document->Text() == "x" + original, "typing routes to editor");
  view->OnEvent(Event::CtrlZ);
  Check(document->Text() == original, "editor undo through root UI");
  view->OnEvent(Event::Special("\x1b[122;6u"));
  Check(document->Text() == "x" + original, "editor redo through root UI");
  view->OnEvent(Event::CtrlZ);
  view->OnEvent(Event::ArrowRight);
  const auto position = document->Cursor();
  view->OnEvent(Event::CtrlC);
  Check(exits == 0, "Ctrl+C in focused editor copies instead of exiting");
  view->OnEvent(Event::F6);
  view->OnEvent(Event::Character('h'));
  screen = Draw(view);
  Check(screen.cursor().y == 21 && document->Cursor() == position,
        "F6 moves focus to input without moving editor caret");
  view->OnEvent(Event::F6);
  screen = Draw(view);
  Check(document->Cursor() == position && screen.cursor().y < 19,
        "F6 restores editor focus and caret");
  Check(Line(screen, 19).find("hello") == std::string::npos,
        "command completion hides while editing code");
  view->OnEvent(Event::Escape);
  Check(exits == 0, "escape does not exit editor or application");
  view->OnEvent(Event::F6);
  view->OnEvent(Event::CtrlC);
  Check(exits == 1, "Ctrl+C in command input still exits");
  std::ifstream unchanged(path, std::ios::binary);
  const std::string disk{std::istreambuf_iterator<char>(unchanged),
                         std::istreambuf_iterator<char>()};
  unchanged.close();
  Check(disk == original, "editing never implicitly writes user settings");
  write("{\"secret-example\": INVALID}");
  loaded = ptoolkit::LoadClaudeConfig(path);
  Check(!loaded && loaded.error().find("secret-example") == std::string::npos,
        "JSON error omits source excerpts");
  app.ExecuteCommand("claude_code_config_man", path);
  screen = Draw(view);
  Check(!app.GetClaudeDocument() &&
            Line(screen, 4).find("Invalid") != std::string::npos &&
            Line(screen, 4).find("JSON") != std::string::npos,
        "invalid JSON displays an error instead of crashing");
  write("[]");
  Check(!ptoolkit::LoadClaudeConfig(path),
        "configuration root must be an object");
  write("{\"value\": 1e9999}");
  Check(!ptoolkit::LoadClaudeConfig(path), "out-of-range JSON errors handled");
  write("\xef\xbb\xbf{\r\n    \"value\": 1\r\n}\r\n");
  loaded = ptoolkit::LoadClaudeConfig(path);
  Check(loaded && loaded->document->Line(0) == "{",
        "UTF-8 BOM and CRLF supported");
  loaded->document->SetCursor({0, 1});
  loaded->document->Enter();
  Check(loaded->document->Line(1) == "    ",
        "existing four-space indentation inferred");
  std::filesystem::remove(path);
  Check(!ptoolkit::LoadClaudeConfig(path), "missing settings handled");
  auto default_path = ptoolkit::ClaudeSettingsPath();
  Check(default_path && default_path->filename() == "settings.json" &&
            default_path->parent_path().filename() == ".claude",
        "default user settings path");
  app.ExecuteCommand("home");
}

void PresetsAndSaving()
{
  const auto root =
      std::filesystem::current_path() /
      ("config-management-fixture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup
  {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{root};
  const auto path = root / "settings.json";
  const auto presets = root / "presets";
  const std::string original =
      "{\n  \"model\": \"old\",\n  \"other\": \"old\"\n}\n";
  Check(ptoolkit::WriteConfigText(path, original).has_value(),
        "write settings fixture");
  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("home");
  int exits = 0;
  auto view =
      ptoolkit::CreateApplicationView(app, [&] { ++exits; }, path, presets);
  Draw(view);
  view->OnEvent(Event::Character("claude_code_config_man"));
  view->OnEvent(Event::Return);
  Draw(view);
  auto document = app.GetClaudeDocument();
  view->OnEvent(Event::Character(' '));
  view->OnEvent(Event::CtrlS);
  Check(
      ptoolkit::ReadConfigText(path) == " " + original && !document->Modified(),
      "Ctrl+S writes edited content to originally loaded settings and marks "
      "saved");
  view->OnEvent(Event::CtrlZ);
  Check(document->Text() == original && document->Modified(),
        "undo after save changes dirty state");
  view->OnEvent(Event::CtrlS);
  document->SetCursor({1, 12});
  view->OnEvent(Event::CtrlD);
  view->OnEvent(Event::CtrlD);
  const auto selections = document->Selections();
  const auto cursor = document->Cursor();
  const auto stable = [&]
  {
    return document->Selections() == selections &&
           document->Cursor() == cursor && document->Text() == original;
  };
  view->OnEvent(Event::CtrlP);
  auto screen = Draw(view);
  Check(Line(screen, 5).find("New preset") != std::string::npos && stable(),
        "Ctrl+P opens left overlay while preserving multiple selections and "
        "cursor");
  Check(Line(screen, 4).find("╭") != std::string::npos &&
            Line(screen, 18).find("╰") != std::string::npos,
        "sidebar spans exactly the editor height");
  Check(screen.CellAt(2, 5).background_color == Color::RGB(0x63, 0xa8, 0xf8),
        "sidebar selection matches command completion color");
  view->OnEvent(Event::Return);
  screen = Draw(view);
  int background_cells = 0, white_borders = 0;
  for (int y = 0; y < screen.dimy(); ++y)
    for (int x = 0; x < screen.dimx(); ++x)
    {
      const auto& cell = screen.CellAt(x, y);
      if (cell.background_color == Color::RGB(0x54, 0x84, 0xb9))
      {
        ++background_cells;
        if (cell.foreground_color == Color::White) ++white_borders;
      }
    }
  Check(background_cells > 100 && white_borders > 20,
        "custom modal has requested blue background and white border");
  view->OnEvent(Event::Character("fast"));
  view->OnEvent(Event::Return);
  Check(ptoolkit::ReadConfigText(presets / "fast.json") == original &&
            stable() && !document->Modified(),
        "new preset stores current content without editing document or saved "
        "state");
  screen = Draw(view);
  Check(
      Line(screen, 7).find("fast") != std::string::npos &&
          screen.CellAt(2, 7).background_color == Color::RGB(0x63, 0xa8, 0xf8),
      "new preset is immediately selected in the list");
  view->OnEvent(Event::Home);
  view->OnEvent(Event::Return);
  view->OnEvent(Event::Character("fast"));
  view->OnEvent(Event::Return);
  screen = Draw(view);
  const auto contains = [&](Screen& image, std::string_view text)
  {
    for (int y = 0; y < image.dimy(); ++y)
      if (Line(image, y).find(text) != std::string::npos) return true;
    return false;
  };
  Check(contains(screen, "already exists") && stable(),
        "duplicate preset rejected with modal error");
  view->OnEvent(Event::Escape);
  screen = Draw(view);
  Check(contains(screen, "already exists"), "error dialog waits for Enter");
  view->OnEvent(Event::Return);
  view->OnEvent(Event::Return);
  view->OnEvent(Event::Character("cancelled"));
  view->OnEvent(Event::Escape);
  Check(!std::filesystem::exists(presets / "cancelled.json") && stable(),
        "Escape cancels new preset without edits");
  view->OnEvent(Event::ArrowDown);
  view->OnEvent(Event::Character('r'));
  view->OnEvent(Event::Escape);
  Check(std::filesystem::exists(presets / "fast.json") && stable(),
        "Escape cancels rename");
  view->OnEvent(Event::Character('r'));
  for (int i = 0; i < 4; ++i) view->OnEvent(Event::Backspace);
  view->OnEvent(Event::Character("renamed"));
  view->OnEvent(Event::Return);
  Check(!std::filesystem::exists(presets / "fast.json") &&
            ptoolkit::ReadConfigText(presets / "renamed.json") == original &&
            stable(),
        "r prompts for rename and Enter renames only the preset file");
  view->OnEvent(Event::Character('d'));
  view->OnEvent(Event::Escape);
  Check(std::filesystem::exists(presets / "renamed.json") && stable(),
        "Escape cancels delete confirmation");
  view->OnEvent(Event::Character('d'));
  view->OnEvent(Event::Return);
  Check(!std::filesystem::exists(presets / "renamed.json") && stable(),
        "Enter confirms deletion without changing editor selection");

  ptoolkit::PresetStore store(presets);
  const std::string replacement =
      "{\n  \"model\": \"new\",\n  \"other\": \"new\"\n}\n";
  Check(store.Create("model", replacement).has_value(),
        "create replacement fixture");
  view->OnEvent(Event::CtrlP);
  view->OnEvent(Event::CtrlP);
  Draw(view);
  view->OnEvent(Event::ArrowDown);
  view->OnEvent(Event::Return);
  Check(
      document->Text() == replacement && document->Selections() == selections &&
          document->Cursor() == cursor && document->Modified(),
      "Enter loads preset preserving all valid caret and selection positions");
  Check(ptoolkit::ReadConfigText(path) == original,
        "loading preset does not implicitly save settings");
  view->OnEvent(Event::CtrlS);
  Check(ptoolkit::ReadConfigText(path) == replacement && !document->Modified(),
        "save shortcut also works with sidebar focused");
  view->OnEvent(Event::CtrlP);
  view->OnEvent(Event::CtrlZ);
  Check(document->Text() == original && document->Selections() == selections,
        "loading preset undoes in one step with selections restored");
  std::filesystem::rename(path, root / "backup.json");
  std::filesystem::create_directory(path);
  view->OnEvent(Event::CtrlS);
  screen = Draw(view);
  Check(contains(screen, "Save failed") && document->Modified() &&
            ptoolkit::ReadConfigText(root / "backup.json") == replacement,
        "failed publish reports error and preserves old contents and dirty "
        "state");
  view->OnEvent(Event::Character('x'));
  view->OnEvent(Event::F6);
  view->OnEvent(Event::CtrlC);
  Check(document->Text() == original && exits == 0,
        "modal captures typing and application shortcuts");
  view->OnEvent(Event::Return);
  screen = Draw(view);
  Check(!contains(screen, "Save failed") && document->Cursor() == cursor,
        "Enter closes error and restores editor focus/caret");
  std::filesystem::remove(path);
  std::filesystem::rename(root / "backup.json", path);
  view->OnEvent(Event::CtrlS);
  Check(ptoolkit::ReadConfigText(path) == original && !document->Modified(),
        "save recovers after failure");

  Check(!store.Create("model", "replacement") &&
            store.Load("model") == replacement,
        "duplicate create never overwrites preset");
  Check(store.Create("other", "other").has_value() &&
            !store.Rename("model", "other") &&
            store.Load("model") == replacement &&
            store.Load("other") == "other",
        "duplicate rename preserves both source and destination");
  for (const auto* invalid :
       {"", "../outside", "a/b", "a\\b", "CON", "nul.json", "bad:", "trailing.",
        " name", "name "})
    Check(!store.Create(invalid, "test"),
          "unsafe/invalid preset name rejected");
  const std::string unicode = "\xe6\xa8\xa1\xe5\x9e\x8b";
  Check(store.Create(unicode, replacement).has_value() &&
            store.Load(unicode) == replacement,
        "Unicode preset names round trip");
  const auto listed = store.List();
  Check(listed && listed->size() == 3, "list contains preset JSON files only");
  for (const auto& entry : std::filesystem::directory_iterator(root))
    Check(!entry.path().filename().string().starts_with(".ptoolkit-write-"),
          "temporary save file cleaned up after failures");
  app.ExecuteCommand("home");
}

void ClaudePrompt()
{
  const auto root =
      std::filesystem::current_path() /
      ("prompt-fixture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup
  {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{root};
  const auto path = root / "CLAUDE.md";
  const std::string original =
      "# Global rules\n\nBe concise. Use \"quotes\", [brackets] and 42.\n";
  Check(ptoolkit::WriteConfigText(path, original, false).has_value(),
        "write prompt fixture");

  auto loaded = ptoolkit::LoadClaudePrompt(path);
  Check(loaded && loaded->path == path &&
            loaded->document->Text() == original,
        "prompt loads as plain text without JSON validation");
  auto missing = ptoolkit::LoadClaudePrompt(root / "nested" / "CLAUDE.md");
  Check(missing && missing->document->Text().empty(),
        "missing prompt opens an empty document");

  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("home");
  int exits = 0;
  auto view = ptoolkit::CreateApplicationView(app, [&] { ++exits; }, {}, {},
                                              path);
  Draw(view);
  view->OnEvent(Event::Character("claude_code_prompt_man"));
  view->OnEvent(Event::Return);
  auto screen = Draw(view);
  auto document = app.GetClaudePromptDocument();
  Check(app.GetPage() == ptoolkit::App::Page::ClaudeCodePrompt && document,
        "prompt command opens the global prompt");
  Check(document->Text() == original, "prompt editor shows CLAUDE.md contents");
  Check(Line(screen, 3).find("~/.claude/CLAUDE.md") != std::string::npos,
        "prompt page labels the global prompt path");
  bool found_hash = false, plain_hash = false;
  for (int y = 3; y < screen.dimy(); ++y)
    for (int x = 0; x < screen.dimx(); ++x)
      if (screen.CellAt(x, y).character == "#")
      {
        found_hash = true;
        plain_hash = screen.CellAt(x, y).foreground_color ==
                     Color::RGB(0xd4, 0xd4, 0xd4);
      }
  Check(found_hash && plain_hash,
        "markdown prose renders without JSON error highlighting");
  Check(screen.cursor().y >= 4 && screen.cursor().y < 19,
        "prompt editor receives focus");
  view->OnEvent(Event::Character('x'));
  Check(document->Text() == "x" + original, "typing routes to prompt editor");
  view->OnEvent(Event::CtrlZ);
  Check(document->Text() == original, "prompt editor undo works");
  view->OnEvent(Event::Character('!'));
  view->OnEvent(Event::CtrlS);
  Check(ptoolkit::ReadConfigText(path) == "!" + original &&
            !document->Modified(),
        "Ctrl+S writes the global prompt");
  view->OnEvent(Event::CtrlC);
  Check(exits == 0, "Ctrl+C in focused prompt editor does not exit");

  const auto created = root / "nested" / "CLAUDE.md";
  app.ExecuteCommand("home");
  auto created_view =
      ptoolkit::CreateApplicationView(app, [] {}, {}, {}, created);
  Draw(created_view);
  created_view->OnEvent(Event::Character("claude_code_prompt_man"));
  created_view->OnEvent(Event::Return);
  document = app.GetClaudePromptDocument();
  Check(document && document->Text().empty(),
        "missing global prompt opens an editable empty buffer");
  created_view->OnEvent(Event::Character("# New"));
  created_view->OnEvent(Event::CtrlS);
  Check(ptoolkit::ReadConfigText(created) == "# New",
        "saving creates a missing CLAUDE.md and its directory");
  app.ExecuteCommand("home");
}

void PromptPresetsAndSaving()
{
  const auto root =
      std::filesystem::current_path() /
      ("prompt-management-fixture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup
  {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{root};
  const auto path = root / "CLAUDE.md";
  const auto presets = root / "presets";
  const std::string original = "# Global rules\n\nBe concise.\n";
  Check(ptoolkit::WriteConfigText(path, original).has_value(),
        "write prompt preset fixture");
  std::filesystem::create_directories(presets);
  Check(ptoolkit::WriteConfigText(presets / "ignore.json", "not a prompt")
            .has_value(),
        "write non-prompt preset fixture");

  auto& app = ptoolkit::App::GetInstance();
  app.ExecuteCommand("home");
  auto view = ptoolkit::CreateApplicationView(app, [] {}, {}, {}, path,
                                              presets);
  Draw(view);
  view->OnEvent(Event::Character("claude_code_prompt_man"));
  view->OnEvent(Event::Return);
  Draw(view);
  auto document = app.GetClaudePromptDocument();
  Check(document != nullptr, "prompt preset session opens CLAUDE.md");
  view->OnEvent(Event::Character(' '));
  view->OnEvent(Event::CtrlS);
  Check(ptoolkit::ReadConfigText(path) == " " + original &&
            !document->Modified(),
        "prompt save keeps the original global file target");

  view->OnEvent(Event::CtrlP);
  view->OnEvent(Event::Return);
  view->OnEvent(Event::Character("style"));
  view->OnEvent(Event::Return);
  Check(ptoolkit::ReadConfigText(presets / "style.md") == " " + original &&
            !std::filesystem::exists(presets / "style.json"),
        "prompt preset is stored as <name>.md");
  ptoolkit::PresetStore prompt_store(presets, ".md");
  auto names = prompt_store.List();
  Check(names && names->size() == 1 && names->front() == "style",
        "prompt preset listing ignores other extensions");

  view->OnEvent(Event::Character('r'));
  for (int i = 0; i < 5; ++i) view->OnEvent(Event::Backspace);
  view->OnEvent(Event::Character("renamed"));
  view->OnEvent(Event::Return);
  Check(!std::filesystem::exists(presets / "style.md") &&
            ptoolkit::ReadConfigText(presets / "renamed.md") == " " + original,
        "prompt preset rename changes only the preset file");

  view->OnEvent(Event::CtrlP);
  view->OnEvent(Event::Character('x'));
  view->OnEvent(Event::CtrlP);
  view->OnEvent(Event::Return);
  Check(document->Text() == " " + original && document->Modified(),
        "prompt preset load replaces editor contents without saving");
  Check(ptoolkit::ReadConfigText(path) == " " + original,
        "loading a prompt preset does not write CLAUDE.md");

  view->OnEvent(Event::Character('d'));
  view->OnEvent(Event::Return);
  Check(!std::filesystem::exists(presets / "renamed.md"),
        "prompt preset delete removes the .md file");
  app.ExecuteCommand("home");
}
}  // namespace

int main()
{
  CompletionAndPages();
  WrappingAndEditing();
  IndependentPageScrolling();
  ClaudeConfiguration();
  PresetsAndSaving();
  ClaudePrompt();
  PromptPresetsAndSaving();
  std::cout << "All UI tests passed\n";
}
