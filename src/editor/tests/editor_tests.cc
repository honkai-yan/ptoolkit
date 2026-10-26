#include "ptoolkit_editor/document.h"
#include "ptoolkit_editor/editor.h"
#include "ptoolkit_editor/json_highlight.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef RGB
#endif

namespace
{
using namespace ptoolkit::editor;
using namespace ftxui;
void Check(bool value, const char* label)
{
  if (!value)
  {
    std::cerr << "FAIL: " << label << '\n';
    std::exit(1);
  }
}
Screen Draw(const Component& view, int width = 40, int height = 10)
{
  Screen screen(width, height);
  Render(screen, view->Render());
  return screen;
}
std::string Row(Screen& screen, int y)
{
  std::string text;
  for (int x = 0; x < screen.dimx(); ++x) text += screen.CellAt(x, y).character;
  return text;
}

void Editing()
{
  Document document;
  document.Type('{');
  Check(document.Text() == "{}" && document.Cursor() == Position{0, 1},
        "auto brace");
  document.Enter();
  Check(document.Text() == "{\n  \n}" && document.Cursor() == Position{1, 2},
        "paired brace newline and indent");
  document.Type('"');
  document.Type('x');
  document.Type('"');
  Check(document.Line(1) == "  \"x\"" && document.Cursor().column == 5,
        "quotes pair and skip closing quote");
  document.Insert(": [1, true]");
  const auto complete = document.Text();
  Check(document.Undo() && document.Line(1) == "  \"x\"",
        "paste undo is atomic");
  Check(document.Redo() && document.Text() == complete, "paste redo");
  while (document.Undo())
  {
  }
  Check(document.Text().empty() && !document.Modified(),
        "all edits undo to clean initial document");
  while (document.Redo())
  {
  }
  Check(document.Text() == complete, "all edits redo");
  document.Undo();
  document.Type('!');
  Check(!document.Redo(), "new edit invalidates redo branch");
  document.MarkSaved();
  Check(!document.Modified(), "mark saved");
  document.Type('a');
  document.Undo();
  Check(!document.Modified(), "undo restores saved state");
  for (char open : std::string("({[\""))
  {
    Document pair;
    pair.Type(open);
    pair.Backspace();
    Check(pair.Text().empty(), "backspace removes an empty auto-pair");
  }
  Document string("\"abc\"");
  string.SetCursor({0, 2});
  string.Type('{');
  Check(string.Text() == "\"a{bc\"", "no brackets paired inside string");
  string.Insert("\\");
  string.Type('"');
  Check(string.Text().find("\\\"") != std::string::npos,
        "escaped quote remains literal");
  Document indent("    \"key\": {\n    }", "    ");
  indent.SetCursor({0, indent.Line(0).size()});
  indent.Enter();
  Check(indent.Line(1) == "        ", "indent unit and existing indentation");
  indent.Type('}');
  Check(indent.Line(1) == "    }", "closing brace dedents one level");
  Document ordinary("  \"name\": \"test\",");
  ordinary.Move(Movement::End);
  ordinary.Enter();
  Check(ordinary.Line(1) == "  ", "ordinary Enter retains indentation");
  Document normalized("a\r\nb\rc\n");
  Check(normalized.Text() == "a\nb\nc\n" && normalized.LineCount() == 4,
        "line endings and final newline");
  Document utf8("\xe4\xbd\xa0\xe5\xa5\xbd");
  utf8.Move(Movement::End);
  utf8.Backspace();
  Check(utf8.Text() == "\xe4\xbd\xa0", "backspace does not split UTF-8");
}

void Navigation()
{
  Document document("foo.bar baz_qux  ++ yes\n  short\nlong line");
  document.Move(Movement::WordRight);
  Check(document.Cursor() == Position{0, 3}, "word end: regular word");
  document.Move(Movement::WordRight);
  Check(document.Cursor().column == 7,
        "word end skips single adjoining separator");
  document.Move(Movement::WordLeft);
  Check(document.Cursor().column == 4, "word start from end of regular word");
  document.Move(Movement::WordLeft);
  Check(document.Cursor().column == 0,
        "WordStartFast skips single adjoining separator");
  document.SetCursor({0, 8});
  document.Move(Movement::WordRight);
  Check(document.Cursor().column == 15, "underscore is not a word separator");
  document.Move(Movement::WordRight);
  Check(document.Cursor().column == 19, "separator groups remain distinct");
  document.Move(Movement::WordRight);
  Check(document.Cursor().column == 23, "skip whitespace to next word end");
  document.Move(Movement::WordRight);
  Check(document.Cursor() == Position{1, 7}, "word-right crosses newline");
  document.Move(Movement::Home);
  document.Move(Movement::WordLeft);
  Check(document.Cursor() == Position{0, 20}, "word-left crosses newline");
  document.Move(Movement::DocumentEnd);
  Check(document.Cursor() == Position{2, 9}, "document end");
  document.Move(Movement::Home);
  Check(document.Cursor() == Position{2, 0}, "line home");
  document.Move(Movement::End);
  document.Move(Movement::Up);
  document.Move(Movement::Up);
  Check(document.Cursor() == Position{0, 9},
        "vertical moves retain preferred column");
  document.Move(Movement::DocumentHome);
  Check(document.Cursor() == Position{}, "document home");
  document.Move(Movement::Left);
  Check(document.Cursor() == Position{}, "left at document boundary");
}

void ComponentAndHighlight()
{
  auto document = std::make_shared<Document>(
      "{\n  \"key\": \"value\",\n  \"n\": -1.5e+2,\n  \"ok\": true,\n  "
      "\"empty\": null\n}");
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document);
  auto screen = Draw(editor);
  Check(Row(screen, 0).find("1 {") != std::string::npos, "line numbers");
  Check(screen.CellAt(5, 1).foreground_color ==
            JsonTokenColor(TokenKind::Property),
        "property highlight");
  Check(screen.CellAt(12, 1).foreground_color ==
            JsonTokenColor(TokenKind::String),
        "string highlight");
  Check(screen.CellAt(10, 2).foreground_color ==
            JsonTokenColor(TokenKind::Number),
        "number highlight");
  Check(screen.CellAt(11, 3).foreground_color ==
            JsonTokenColor(TokenKind::Keyword),
        "boolean highlight");
  const auto tokens = HighlightJsonLine("\"a\\\"b\":false");
  Check(tokens[0].kind == TokenKind::Property &&
            tokens.back().kind == TokenKind::Keyword,
        "escaped string lexing");
  editor->OnEvent(Event::Character('x'));
  editor->OnEvent(Event::CtrlZ);
  Check(document->Line(0) == "{", "component undo binding");
  editor->OnEvent(Event::Special("\x1b[122;6u"));
  Check(document->Line(0) == "x{", "Ctrl+Shift+Z redo CSI-u binding");
  editor->OnEvent(Event::Special("\x1b[1;5F"));
  Check(document->Cursor() == Position{5, 1}, "Ctrl+End binding");
  editor->OnEvent(Event::Special("\x1b[1;5H"));
  Check(document->Cursor() == Position{}, "Ctrl+Home binding");
  auto other = Button("other", [] {});
  auto container = Container::Vertical({editor, other});
  editor->TakeFocus();
  editor->OnEvent(Event::ArrowRight);
  const auto cursor = document->Cursor();
  other->TakeFocus();
  Draw(container);
  editor->TakeFocus();
  Check(document->Cursor() == cursor, "focus loss preserves cursor");
  Check(document->Line(0) == "x{", "focus loss preserves content");
  std::string many_lines;
  for (int i = 0; i < 100; ++i)
    many_lines += "\"line\": 123456789012345678901234567890,\n";
  auto big = std::make_shared<Document>(many_lines);
  auto scrolling = std::make_shared<ptoolkit::editor::Editor>(big);
  scrolling->OnEvent(Event::Special("\x1b[1;5F"));
  screen = Draw(scrolling, 20, 6);
  Check(
      Row(screen, 5).find("101") != std::string::npos && screen.cursor().y == 5,
      "vertical viewport follows cursor");
  big->SetCursor({99, 35});
  screen = Draw(scrolling, 20, 6);
  Check(screen.cursor().x < 20, "horizontal viewport follows long line");
  screen = Draw(scrolling, 8, 3);
  Check(screen.cursor().x < 8 && screen.cursor().y < 3,
        "resize keeps cursor visible");
}

#if defined(__APPLE__)
void MacWordNavigation()
{
  auto document = std::make_shared<Document>("alpha beta gamma");
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document);

  // Option+arrow uses the xterm Alt modifier sequences.
  editor->OnEvent(Event::Special("\x1b[1;3C"));
  Check(document->Cursor() == Position{0, 5}, "Option+Right: word end");
  editor->OnEvent(Event::Special("\x1b[1;3C"));
  Check(document->Cursor() == Position{0, 10},
        "Option+Right skips separator and stops at next word end");
  editor->OnEvent(Event::Special("\x1b[1;3D"));
  Check(document->Cursor() == Position{0, 6}, "Option+Left: word start");

  // iTerm2 natural text editing and Terminal.app Option-as-Meta send ESC b/f.
  editor->OnEvent(Event::Special("\x1b""f"));
  Check(document->Cursor() == Position{0, 10}, "Meta+f moves word right");
  editor->OnEvent(Event::Special("\x1b""b"));
  Check(document->Cursor() == Position{0, 6}, "Meta+b moves word left");

  // Option+Shift+arrow extends the selection one word at a time.
  document->ClearSelection();
  document->SetCursor({0, 0});
  editor->OnEvent(Event::Special("\x1b[1;4C"));
  Check(document->SelectedText() == "alpha",
        "Option+Shift+Right selects a word");
  editor->OnEvent(Event::Special("\x1b[1;4C"));
  Check(document->SelectedText() == "alpha beta",
        "repeated Option+Shift+Right extends the selection");
  editor->OnEvent(Event::Special("\x1b[1;4D"));
  Check(document->SelectedText() == "alpha ",
        "Option+Shift+Left shrinks the selection by a word");

  document->ClearSelection();
  document->SetCursor({0, 10});
  editor->OnEvent(Event::Special("\x1b""B"));
  Check(document->SelectedText() == "beta", "Meta+B selects the previous word");
  document->ClearSelection();
  document->SetCursor({0, 0});
  editor->OnEvent(Event::Special("\x1b""F"));
  Check(document->SelectedText() == "alpha", "Meta+F selects the next word");

  // Existing Windows-style bindings remain usable on macOS.
  document->ClearSelection();
  document->SetCursor({0, 0});
  editor->OnEvent(Event::Special("\x1b[1;5C"));
  Check(document->Cursor() == Position{0, 5}, "Ctrl+Right remains available");
}
#endif

class MemoryClipboard : public Clipboard
{
public:
  std::string text;
  int writes = 0;
  bool fail_read = false, fail_write = false;
  std::expected<void, std::string> Write(std::string_view value) override
  {
    if (fail_write) return std::unexpected("Clipboard busy");
    ++writes;
    text = value;
    return {};
  }
  std::expected<std::string, std::string> Read() override
  {
    if (fail_read) return std::unexpected("Clipboard busy");
    return text;
  }
};

void SelectionsAndClipboard()
{
  Document document("abc\ndef\nghi");
  document.SetCursor({0, 1});
  document.SetCursor({1, 2}, true);
  Check(document.SelectedText() == "bc\nde" && !document.Modified(),
        "multiline selection is not an edit");
  document.Insert("X\r\nY");
  Check(document.Text() == "aX\nYf\nghi" && !document.HasSelection(),
        "paste atomically replaces selection");
  Check(document.Undo() && document.Text() == "abc\ndef\nghi" &&
            document.SelectedText() == "bc\nde",
        "undo restores text and selection");
  Check(document.Redo() && document.Text() == "aX\nYf\nghi" &&
            !document.HasSelection(),
        "redo replacement");
  document.Undo();
  document.Backspace();
  Check(document.Text() == "af\nghi", "backspace deletes selected range");
  document.Undo();
  document.Delete();
  Check(document.Text() == "af\nghi", "delete deletes selected range");
  document.Undo();
  document.Type('!');
  Check(document.Text() == "a!f\nghi", "typing replaces selection");
  document.SetText("abc\ndef");
  document.SetCursor({1, 2});
  document.SetCursor({0, 1}, true);
  Check(document.SelectedText() == "bc\nde",
        "reverse selection normalizes range");
  document.Move(Movement::Right);
  Check(!document.HasSelection() && document.Cursor() == Position{1, 2},
        "right collapses to selection end");
  document.Move(Movement::Left, true);
  document.Move(Movement::Left);
  Check(document.Cursor() == Position{1, 1} && !document.HasSelection(),
        "left collapses to selection start");
  document.SelectAll();
  document.Enter();
  Check(document.Text() == "\n",
        "Enter replaces selected content in one transaction");
  document.Undo();
  Check(document.SelectedText() == "abc\ndef", "undo Enter restores selection");

  auto buffer = std::make_shared<Document>("abc\ndef\nghi");
  auto clipboard = std::make_shared<MemoryClipboard>();
  auto editor = std::make_shared<ptoolkit::editor::Editor>(buffer, clipboard);
  Draw(editor);
  buffer->SetCursor({0, 1});
  editor->OnEvent(Event::Special("\x1b[1;2C"));
  Check(buffer->SelectedText() == "b", "Shift+Right selects");
  auto screen = Draw(editor);
  Check(screen.CellAt(4, 0).background_color == Color::RGB(0x26, 0x4f, 0x78),
        "selection renders with VS Code background");
  editor->OnEvent(Event::CtrlC);
  Check(clipboard->text == "b" && clipboard->writes == 1 &&
            buffer->SelectedText() == "b",
        "copy retains selection");
  clipboard->text = "\xe4\xbd\xa0\xe5\xa5\xbd\r\nnew";
  editor->OnEvent(Event::CtrlV);
  Check(buffer->Text() == "a\xe4\xbd\xa0\xe5\xa5\xbd\nnewc\ndef\nghi",
        "UTF-8 multiline paste replaces selection");
  editor->OnEvent(Event::CtrlZ);
  Check(buffer->Text() == "abc\ndef\nghi" && buffer->SelectedText() == "b",
        "paste undoes in one step");
  clipboard->fail_read = true;
  editor->OnEvent(Event::CtrlV);
  Check(!editor->ClipboardError().empty() && buffer->SelectedText() == "b" &&
            buffer->Text() == "abc\ndef\nghi",
        "failed paste preserves selection and content");
  clipboard->fail_read = false;
  clipboard->text.clear();
  editor->OnEvent(Event::CtrlV);
  Check(buffer->SelectedText() == "b",
        "empty clipboard does not delete selection");
  clipboard->fail_write = true;
  editor->OnEvent(Event::CtrlC);
  Check(!editor->ClipboardError().empty() && buffer->SelectedText() == "b",
        "copy failure is reported without editing");
  clipboard->fail_write = false;
  buffer->ClearSelection();
  editor->OnEvent(Event::CtrlC);
  Check(clipboard->writes == 1, "Ctrl+C without selection is a handled no-op");
  editor->OnEvent(Event::CtrlA);
  Check(buffer->SelectedText() == buffer->Text(), "Ctrl+A selects document");
  editor->OnEvent(Event::Special("\x1b[1;5H"));
  editor->OnEvent(Event::Special("\x1b[1;6C"));
  Check(buffer->SelectedText() == "abc", "Ctrl+Shift+Right selects by word");
  editor->OnEvent(Event::Special("\x1b[1;6F"));
  Check(buffer->SelectedText() == buffer->Text(),
        "Ctrl+Shift+End extends to document end");
  editor->OnEvent(Event::Special("\x1b[1;6H"));
  Check(!buffer->HasSelection(),
        "extending back to anchor clears nonempty selection");

  Mouse mouse;
  mouse.button = Mouse::Left;
  mouse.motion = Mouse::Pressed;
  mouse.x = 4;
  mouse.y = 0;
  editor->OnEvent(Event::Mouse("", mouse));
  mouse.motion = Mouse::Moved;
  mouse.x = 5;
  mouse.y = 1;
  editor->OnEvent(Event::Mouse("", mouse));
  mouse.button = Mouse::None;
  mouse.motion = Mouse::Released;
  editor->OnEvent(Event::Mouse("", mouse));
  Check(buffer->SelectedText() == "bc\nde", "mouse drag selects across lines");
  auto other = Button("other", [] {});
  auto container = Container::Vertical({editor, other});
  editor->TakeFocus();
  other->TakeFocus();
  screen = Draw(container);
  Check(
      buffer->SelectedText() == "bc\nde" &&
          screen.CellAt(4, 0).background_color == Color::RGB(0x3a, 0x3d, 0x41),
      "unfocused selection persists with inactive color");
  editor->TakeFocus();
  mouse.button = Mouse::Left;
  mouse.motion = Mouse::Pressed;
  mouse.shift = true;
  mouse.x = 6;
  mouse.y = 2;
  editor->OnEvent(Event::Mouse("", mouse));
  mouse.button = Mouse::None;
  mouse.motion = Mouse::Released;
  editor->OnEvent(Event::Mouse("", mouse));
  Check(buffer->SelectedText() == "bc\ndef\nghi",
        "Shift+click extends existing anchor");
}

#ifdef _WIN32
void SystemClipboardIsolation()
{
  // Every window station has its own clipboard. This test never reads or writes
  // the interactive user's clipboard, including non-text clipboard formats.
  const auto original_station = GetProcessWindowStation();
  const auto original_desktop = GetThreadDesktop(GetCurrentThreadId());
  const auto station =
      CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
  Check(station != nullptr, "create private clipboard window station");
  Check(SetProcessWindowStation(station),
        "connect to private clipboard station");
  const auto desktop = CreateDesktopW(L"EditorClipboardTest", nullptr, nullptr,
                                      0, GENERIC_ALL, nullptr);
  Check(desktop != nullptr && SetThreadDesktop(desktop),
        "connect private clipboard desktop");
  {
    const auto clipboard = MakeSystemClipboard();
    Check(clipboard->Write("copy \xe4\xbd\xa0\xe5\xa5\xbd\nline 2").has_value(),
          "native clipboard write");
    const auto text = clipboard->Read();
    Check(text && *text == "copy \xe4\xbd\xa0\xe5\xa5\xbd\r\nline 2",
          "native Unicode clipboard roundtrip");
    Check(!clipboard->Write(std::string("bad\0text", 8)),
          "reject NUL before replacing clipboard");
    Check(clipboard->Read() == text, "rejected copy preserves clipboard");
    Check(!clipboard->Write("\xff"),
          "reject invalid UTF-8 without replacing clipboard");
    Check(clipboard->Write("").has_value() &&
              clipboard->Read() == std::expected<std::string, std::string>(""),
          "native empty text roundtrip");
  }
  Check(SetThreadDesktop(original_desktop), "restore original desktop");
  Check(SetProcessWindowStation(original_station),
        "restore original window station");
  CloseDesktop(desktop);
  CloseWindowStation(station);
  std::cout << "Isolated native clipboard tests passed\n";
}
#endif

void RandomizedHistory()
{
  Document document;
  std::string reference;
  std::mt19937 random(13579);
  for (int iteration = 0; iteration < 2000; ++iteration)
  {
    const auto original = reference;
    const auto at = reference.empty() ? 0 : random() % (reference.size() + 1);
    Position cursor;
    for (std::size_t i = 0; i < at; ++i)
      if (reference[i] == '\n')
      {
        ++cursor.line;
        cursor.column = 0;
      }
      else
        ++cursor.column;
    document.SetCursor(cursor);
    if (random() % 3 == 0 && at < reference.size())
    {
      document.Delete();
      reference.erase(at, 1);
    }
    else
    {
      const std::string insert = random() % 4 == 0 ? "a\nb" : "xyz";
      document.Insert(insert);
      reference.insert(at, insert);
    }
    Check(document.Text() == reference,
          "random range edit against flat reference");
    Check(document.Undo() && document.Text() == original,
          "random undo roundtrip");
    Check(document.Redo() && document.Text() == reference,
          "random redo roundtrip");
  }
}

void CutClipboard()
{
  const std::string original = "a\xe4\xbd\xa0\xe5\xa5\xbd\nbcd\nend";
  const std::string selection = "\xe4\xbd\xa0\xe5\xa5\xbd\nbc";
  auto document = std::make_shared<Document>(original);
  auto clipboard = std::make_shared<MemoryClipboard>();
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document, clipboard);
  Draw(editor);
  document->SetCursor({1, 2});
  document->SetCursor({0, 1}, true);
  clipboard->fail_write = true;
  Check(editor->OnEvent(Event::CtrlX) && document->Text() == original &&
            document->SelectedText() == selection && !document->Modified() &&
            !editor->ClipboardError().empty(),
        "failed cut preserves text, selection and history");
  Check(!document->Undo(), "failed cut creates no undo transaction");
  clipboard->fail_write = false;
  editor->OnEvent(Event::CtrlX);
  Check(clipboard->text == selection && clipboard->writes == 1 &&
            document->Text() == "ad\nend" && !document->HasSelection() &&
            document->Cursor() == Position{0, 1} &&
            editor->ClipboardError().empty(),
        "cut copies reversed UTF-8 multiline selection then deletes it");
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == original && document->SelectedText() == selection &&
            document->Cursor() == Position{0, 1} && !document->Modified(),
        "cut undo restores text and selection in one step");
  editor->OnEvent(Event::CtrlY);
  Check(document->Text() == "ad\nend" && !document->HasSelection() &&
            clipboard->writes == 1,
        "cut redo deletes without writing clipboard");
  Check(editor->OnEvent(Event::CtrlX) && document->Text() == "ad\nend" &&
            clipboard->writes == 1 && clipboard->text == selection,
        "cut without selection leaves document and clipboard unchanged");
  editor->OnEvent(Event::CtrlV);
  Check(document->Text() == original, "cut text can be pasted back");
  editor->OnEvent(Event::CtrlA);
  editor->OnEvent(Event::CtrlX);
  Check(document->Text().empty() && clipboard->text == original,
        "cut whole document");
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == original && document->SelectedText() == original,
        "whole-document cut undoes atomically");
}

void NextOccurrenceEditing()
{
  auto document = std::make_shared<Document>("old old\nold OLD older old");
  auto clipboard = std::make_shared<MemoryClipboard>();
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document, clipboard);
  Draw(editor);
  document->SetCursor({1, 1});
  auto next = [&] { Check(editor->OnEvent(Event::CtrlD), "Ctrl+D handled"); };
  next();
  Check(document->SelectedText() == "old" && document->CursorCount() == 1,
        "first Ctrl+D selects current word");
  next();
  Check(document->Cursor() == Position{1, 17} && document->CursorCount() == 2,
        "implicit word matching is case-sensitive and skips word substrings");
  next();
  Check(document->Cursor() == Position{0, 3} && document->CursorCount() == 3,
        "Ctrl+D wraps around EOF");
  next();
  next();
  Check(document->CursorCount() == 4 && !document->Modified() &&
            !document->Undo(),
        "exhausted search keeps all unique selections without editing history");
  auto screen = Draw(editor);
  Check(
      screen.CellAt(3, 0).background_color == Color::RGB(0x26, 0x4f, 0x78) &&
          screen.CellAt(7, 0).background_color ==
              Color::RGB(0x26, 0x4f, 0x78) &&
          screen.CellAt(3, 1).background_color == Color::RGB(0x26, 0x4f, 0x78),
      "all selected occurrences render highlighted");
  editor->OnEvent(Event::CtrlC);
  Check(clipboard->text == "old\nold\nold\nold",
        "copy joins selected ranges in document order");
  editor->OnEvent(Event::Character('n'));
  editor->OnEvent(Event::Character('e'));
  editor->OnEvent(Event::Character('w'));
  Check(document->Text() == "new new\nnew OLD older new" &&
            document->CursorCount() == 4,
        "successive typing edits every occurrence and preserves carets");
  screen = Draw(editor);
  Check(screen.CellAt(6, 0).inverted, "secondary collapsed carets are visible");
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == "ne ne\nne OLD older ne",
        "one key undoes across all carets");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == "old old\nold OLD older old" &&
            document->CursorCount() == 4 && document->SelectedText() == "old" &&
            !document->Modified(),
        "undo restores every selection and clean state");
  editor->OnEvent(Event::CtrlY);
  Check(
      document->Text() == "n n\nn OLD older n" && document->CursorCount() == 4,
      "redo restores all edited carets");
  editor->OnEvent(Event::CtrlZ);
  clipboard->text = "\xe4\xbd\xa0\xe5\xa5\xbd\r\nx";
  editor->OnEvent(Event::CtrlV);
  Check(document->Text() ==
            "\xe4\xbd\xa0\xe5\xa5\xbd\nx \xe4\xbd\xa0\xe5\xa5\xbd\nx\n"
            "\xe4\xbd\xa0\xe5\xa5\xbd\nx OLD older \xe4\xbd\xa0\xe5\xa5\xbd\nx",
        "multiline UTF-8 paste replaces every selected occurrence");
  editor->OnEvent(Event::Character('!'));
  Check(document->Line(1) == "x! \xe4\xbd\xa0\xe5\xa5\xbd" &&
            document->Line(5) == "x!",
        "carets track shifted lines and columns after multiline paste");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::CtrlZ);
  clipboard->fail_write = true;
  editor->OnEvent(Event::CtrlX);
  Check(document->CursorCount() == 4 && document->SelectedText() == "old" &&
            document->Text() == "old old\nold OLD older old",
        "failed multi-cut preserves every selection");
  clipboard->fail_write = false;
  editor->OnEvent(Event::CtrlX);
  Check(document->Text() == " \n OLD older " &&
            clipboard->text == "old\nold\nold\nold",
        "multi-cut writes all ranges then removes them atomically");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::Delete);
  Check(document->Text() == " \n OLD older ", "Delete removes all selections");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::Backspace);
  Check(document->Text() == " \n OLD older ",
        "Backspace removes all selections");
  editor->OnEvent(Event::CtrlZ);
  Check(editor->OnEvent(Event::Escape) && document->CursorCount() == 1 &&
            document->SelectedText() == "old",
        "Escape retains primary selection and removes extra carets");
  next();
  editor->OnEvent(Event::ArrowRight);
  Check(document->CursorCount() == 1 && !document->HasSelection(),
        "ordinary navigation returns to single-caret editing");

  document->SetText("model-v1 model-v10\nmodel-v1");
  document->SetCursor({0, 8});
  document->SetCursor({0, 0}, true);
  next();
  Check(document->CursorCount() == 2 && document->Cursor() == Position{0, 17},
        "explicit selection matches literal substrings including hyphens");
  next();
  editor->OnEvent(Event::Character('X'));
  Check(document->Text() == "X X0\nX",
        "same-line literal replacements use original ranges");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::Return);
  Check(document->Text() == "\n \n0\n\n",
        "Enter replaces all selected occurrences");
  editor->OnEvent(Event::CtrlZ);
  editor->OnEvent(Event::Tab);
  Check(document->Text() == "     0\n  ",
        "Tab replaces all selections with indentation");

  document->SetText("aaaa");
  document->SetCursor({0, 0});
  document->SetCursor({0, 2}, true);
  next();
  next();
  Check(document->CursorCount() == 2,
        "overlapping and already-selected matches are skipped");
  editor->OnEvent(Event::Delete);
  Check(document->Text().empty() && document->CursorCount() == 1,
        "adjacent deletions deduplicate coincident carets");
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == "aaaa" && document->CursorCount() == 2,
        "undo restores adjacent selections");

  document->SetText("a\nb a\nb");
  document->SetCursor({0, 0});
  document->SetCursor({1, 1}, true);
  next();
  Check(document->CursorCount() == 2,
        "explicit multiline occurrence can be selected");
  editor->OnEvent(Event::Character('x'));
  Check(document->Text() == "x x",
        "multiline occurrences replace in one transaction");
  editor->OnEvent(Event::CtrlZ);
  Check(document->Text() == "a\nb a\nb" && document->CursorCount() == 2,
        "multiline transaction undoes");
  document->SetText("");
  next();
  next();
  Check(document->CursorCount() == 1 && !document->HasSelection(),
        "Ctrl+D on empty file is harmless");
}

void ReplaceDocumentText()
{
  Document document("old old\nlast");
  document.SetCursor({0, 1});
  document.SelectNextOccurrence();
  document.SelectNextOccurrence();
  const auto selections = document.Selections();
  const auto cursor = document.Cursor();
  document.ReplaceText("new new\nlast");
  Check(document.Selections() == selections && document.Cursor() == cursor &&
            document.Modified(),
        "replace all text retains all selection positions");
  document.Type('x');
  Check(document.Text() == "x x\nlast",
        "retained selections remain editable after replacement");
  document.Undo();
  document.Undo();
  Check(document.Text() == "old old\nlast" &&
            document.Selections() == selections && !document.Modified(),
        "undo whole-text replacement restores all selections and clean state");
  document.Redo();
  Check(
      document.Text() == "new new\nlast" && document.Selections() == selections,
      "redo whole-text replacement restores after selections");
  document.ReplaceText("");
  Check(document.Cursor() == Position{} && document.CursorCount() == 1,
        "replacing with empty buffer clamps and deduplicates carets");
  document.Undo();
  Check(document.Text() == "new new\nlast" && document.CursorCount() == 2,
        "undo clamped replacement restores all carets");
  document.SetText("a\nbcd");
  document.SetCursor({1, 3});
  document.SetCursor({0, 1}, true);
  document.ReplaceText("\xe4\xbd\xa0");
  Check(document.Cursor() == Position{} &&
            document.Selection().second == Position{0, 3},
        "replacement clamps cursor and anchor to UTF-8 boundaries");
  document.Undo();
  Check(document.SelectedText() == "\nbcd",
        "undo restores backward multiline selection");
}

void MultiClickSelection()
{
  Document ranges("foo_bar  ++ baz\n\nlast\n\xe4\xbd\xa0\xe5\xa5\xbd");
  auto selected = [&](Position at)
  {
    const auto [begin, end] = ranges.WordRange(at);
    ranges.SetCursor(begin);
    ranges.SetCursor(end, true);
    return ranges.SelectedText();
  };
  Check(selected({0, 3}) == "foo_bar", "double-click word keeps underscores");
  Check(selected({0, 7}) == "  ", "double-click whitespace run");
  Check(selected({0, 9}) == "++", "double-click separator run");
  Check(selected({0, 99}) == "baz",
        "double-click beyond end selects last word");
  Check(selected({1, 0}).empty(), "double-click empty line");
  Check(selected({3, 4}) == "\xe4\xbd\xa0\xe5\xa5\xbd",
        "word range preserves UTF-8 boundaries");
  Check(ranges.LineRange(1) == std::pair{Position{1, 0}, Position{2, 0}},
        "empty line includes newline");
  Check(ranges.LineRange(3).second == Position{3, 6}, "last line ends at EOF");

  auto document = std::make_shared<Document>("one two three\nfour five\nlast");
  auto editor = std::make_shared<ptoolkit::editor::Editor>(document);
  Draw(editor);
  auto press = [&](int line, int column, Mouse::Motion motion)
  {
    Mouse mouse;
    mouse.button = motion == Mouse::Released ? Mouse::None : Mouse::Left;
    mouse.motion = motion;
    mouse.x = column + 3;
    mouse.y = line;
    editor->OnEvent(Event::Mouse("", mouse));
  };
  auto click = [&](int line, int column)
  {
    press(line, column, Mouse::Pressed);
    press(line, column, Mouse::Released);
  };
  click(0, 5);
  Check(!document->HasSelection(), "single click places caret");
  click(0, 5);
  Check(document->SelectedText() == "two", "double click selects word");
  click(0, 5);
  Check(document->SelectedText() == "one two three\n",
        "triple click selects line");
  click(0, 5);
  Check(!document->HasSelection(), "fourth click restarts sequence");
  press(0, 5, Mouse::Pressed);
  press(1, 6, Mouse::Moved);
  Check(document->SelectedText() == "two three\nfour five",
        "double click drag extends by whole words");
  press(0, 0, Mouse::Moved);
  Check(document->SelectedText() == "one two",
        "word drag reverses around original word");
  press(0, 0, Mouse::Released);
  click(1, 2);
  click(1, 2);
  press(1, 2, Mouse::Pressed);
  press(2, 1, Mouse::Moved);
  Check(document->SelectedText() == "four five\nlast",
        "triple click drag through EOF");
  press(0, 1, Mouse::Moved);
  Check(document->SelectedText() == "one two three\nfour five\n",
        "line drag reverses around original line");
  press(0, 1, Mouse::Released);
  click(0, 2);
  editor->OnEvent(Event::ArrowRight);
  click(0, 2);
  Check(!document->HasSelection(), "keyboard input breaks click sequence");
  click(0, 2);
  Check(document->SelectedText() == "one", "new double click after keyboard");
  editor->SetDocument(std::make_shared<Document>("replacement"));
  Draw(editor);
  click(0, 2);
  Check(!editor->GetDocument()->HasSelection(),
        "document switch resets click sequence");
}

void Performance()
{
  std::string large;
  large.reserve(8'000'000);
  for (int i = 0; i < 100'000; ++i)
    large +=
        "  \"example_setting\": \"a moderately long configuration value for "
        "performance testing\",\n";
  auto document = std::make_shared<Document>(large);
  const auto unchanged = document->LineVersion(99'999);
  document->SetCursor({50'000, 20});
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i)
  {
    document->Type('x');
    document->Undo();
  }
  const auto editing = std::chrono::steady_clock::now();
  auto view = std::make_shared<ptoolkit::editor::Editor>(document);
  for (int i = 0; i < 100; ++i) Draw(view, 120, 40);
  const auto end = std::chrono::steady_clock::now();
  Check(document->LineVersion(99'999) == unchanged,
        "local edits leave other line caches valid");
  Check(document->LineCount() == 100'001, "large document line count");
  std::cout << "100k lines / " << large.size() << " bytes: 1000 edit+undo="
            << std::chrono::duration_cast<std::chrono::milliseconds>(editing -
                                                                     start)
                   .count()
            << "ms; 100 frames (120x40)="
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     editing)
                   .count()
            << "ms\n";
}
}  // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
  if (argc > 1 && std::string_view(argv[1]) == "--system-clipboard-test")
  {
    SystemClipboardIsolation();
    return 0;
  }
#endif
  Editing();
  Navigation();
  ComponentAndHighlight();
#if defined(__APPLE__)
  MacWordNavigation();
#endif
  SelectionsAndClipboard();
  CutClipboard();
  NextOccurrenceEditing();
  ReplaceDocumentText();
  MultiClickSelection();
  RandomizedHistory();
  Performance();
  std::cout << "All editor tests passed\n";
}
