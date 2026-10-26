# ptoolkit_editor

An embeddable JSON code editor built on **FTXUI >= 7.0.3**, using C++23.
The target is a static library: `ptoolkit_editor` / `ptoolkit::editor`.
This directory can be copied into a separate project unchanged.

## Boundaries

- `Document`: line-indexed UTF-8 text, cursor movement, range edits, undo/redo,
  auto-closing pairs and indentation. Uses only the C++ standard library.
- `json_highlight`: a tolerant, line-local JSON lexer and VS Code Dark+ colors.
- `Editor`: a regular FTXUI `ComponentBase`, including a gutter, caret, scrolling
  and a bounded cache of visible lines. It owns no terminal or event loop.
- `Clipboard`: a small injectable UTF-8 interface. The default Windows backend
  uses the system Unicode clipboard; other platforms can supply a backend.
- File paths, reading/parsing configuration, command routing and application
  state belong to the host application, not this library. In ptoolkit these
  are implemented in `src/claude_config.*`, `src/app.*` and `src/ui.*`.

The public headers are under `include/ptoolkit_editor`. Positions use zero-based
line numbers and UTF-8 **byte** columns. The editor handles display widths;
word navigation uses ASCII character classes, without language segmentation.
Document and component operations run on the UI thread.

## Embed

```cmake
find_package(ftxui CONFIG REQUIRED) # version >= 7.0.3 checked by the module
add_subdirectory(path/to/editor)
target_link_libraries(my_app PRIVATE ptoolkit::editor)
```

```cpp
#include <ptoolkit_editor/editor.h>
#include <ftxui/component/screen_interactive.hpp>

auto document = std::make_shared<ptoolkit::editor::Document>("{}\n");
auto editor = std::make_shared<ptoolkit::editor::Editor>(document);
auto screen = ftxui::ScreenInteractive::Fullscreen();
screen.ForceHandleCtrlZ(false); // Let the component handle undo.
screen.ForceHandleCtrlC(false); // Let the component copy, without forced exit.
screen.Loop(editor);
// document->Text() returns the current buffer; the host decides how to save it.
```

Place the component in the host's container and use `TakeFocus()` to focus it.
Losing focus does not reset the document, cursor, undo history or viewport.
Call `BreakUndoGroup()` when changing focus if a new typing group is desired.
The supplied demo uses F10 to exit, leaving Ctrl+C for copying in the editor.
In the ptoolkit host, F6 focuses the command input, where Ctrl+C still exits.

## Standalone build

With FTXUI installed at a local prefix (same compiler/runtime as the editor):

```powershell
cmake -S src/editor -B build/editor-standalone -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="path/to/ftxui-prefix" `
  -DPTOOLKIT_EDITOR_BUILD_TESTS=ON -DPTOOLKIT_EDITOR_BUILD_EXAMPLE=ON
cmake --build build/editor-standalone
ctest --test-dir build/editor-standalone --output-on-failure
.\build\editor-standalone\ptoolkit_editor_demo.exe
```

The root project can also enable `PTOOLKIT_EDITOR_BUILD_TESTS` and
`PTOOLKIT_EDITOR_BUILD_EXAMPLE`. No network download occurs during configuration.

## Keys

| Key | Operation |
| --- | --- |
| Arrows | Move the caret, scrolling as needed |
| Shift + movement keys | Extend/shrink a selection (arrows, Home/End, PageUp/PageDown) |
| Ctrl+Shift+Left / Right / Home / End | Select by word or to a document boundary |
| Ctrl+A | Select all |
| Ctrl+D | Select current word, then add the next matching occurrence |
| Esc (with multiple carets) | Keep the primary selection/caret only |
| Ctrl+C | Copy selection; with no selection, leave clipboard and document unchanged |
| Ctrl+X | Cut selection as one undo transaction; with no selection, do nothing |
| Ctrl+V | Paste text, replacing selection, as one undo transaction |
| Ctrl+Left / Ctrl+Right | VS Code Windows WordStartFast / WordEnd behavior |
| Home / End | Start / end of the current line |
| Ctrl+Home / Ctrl+End | Start / end of the document |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z | Redo (Ctrl+Y is also accepted) |
| Enter | New line with inherited/smart indentation |
| Tab | Insert one indentation unit |
| Backspace / Delete | Delete text / join lines |
| PageUp / PageDown | Move the caret by a viewport |
| Mouse wheel | Scroll without moving the caret |
| Left click | Focus and place the caret |
| Double click / triple click | Select a word / entire line (including its newline) |
| Double/triple click then drag | Extend selection by whole words / lines |
| Left drag / Shift+click | Select text / extend the existing anchor |

`(`, `{`, `[` and double quotes are paired outside strings. Typing an existing
closing delimiter advances the cursor. Enter between a pair creates an indented
inner line; closing a brace/bracket on an indentation-only line dedents it.
Pastes submitted as a string use a single undo transaction. Consecutive ordinary
typing is grouped until a movement or other edit breaks the group.
Selection survives focus changes; an inactive selection has a dimmer background.
Repeated clicks at the same cell within 500 ms select a word, then a line;
the fourth click starts a new sequence. Words use the same separator classes as
word navigation; whitespace and punctuation runs can also be selected.
Typing, Backspace and Delete replace/remove the selected range. Undo restores
both the previous text and its selection. Copy preserves the selection.
Cut deletes the selection only after a successful clipboard write; clipboard
failure leaves the text and selection intact. Undo restores the cut selection.

Ctrl+D searches case-sensitively, wraps at EOF and skips overlapping/already
selected occurrences. Starting without a selection uses whole-word matches;
an explicitly selected range uses literal matches (including multiline text).
Typing, paste, Enter, Tab, Backspace, Delete and cut act on all selected ranges
and keep carets for continued typing. Each action is one undo/redo transaction,
stored as range deltas rather than document snapshots. All ranges are highlighted;
extra collapsed carets are drawn as inverted cells. Copy/cut joins nonempty
ranges with newlines in document order; paste inserts the same clipboard text
at each caret. Focus changes preserve all selections. Clicking, ordinary movement,
or Escape returns to the primary caret; Ctrl+A replaces them with select-all.
The Windows example target `ptoolkit_editor_multiselect_probe` checks native
Ctrl+D, continued typing at all carets, and undo/redo in a real console.

Clipboard failures are exposed through `Editor::ClipboardError()` so the host
can display them. Failed/empty pastes preserve the current document and selection.
The Windows backend converts UTF-8 to `CF_UNICODETEXT` (UTF-16 and CRLF) on copy;
the document normalizes pasted line endings back to LF. It creates a message-only
owner window lazily on the UI thread, as required by
[OpenClipboard / SetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard).
No shell commands or clipboard helper processes are used.

### Windows input adapter

Unmodified FTXUI 7.0.3 enables VT input and discards native modifier information.
On the tested Windows console this collapsed Ctrl+Shift+Z into Ctrl+Z.
The parent project ships a documented Windows-only input patch in
`3rdparty/patches/ftxui-7.0.3-native-input.patch`: it keeps native key records,
encodes special keys/modifiers, and forwards native mouse events. The original
console mode is restored by FTXUI on exit. No other platform is changed.
The patch also disables processed console input while the loop is running, so
Ctrl+C reaches the focused editor instead of generating an unconditional signal.
Hosts must set `ForceHandleCtrlC(false)` and route Ctrl+C according to focus.

When extracting the library, use this patched FTXUI on Windows or another input
provider that distinguishes these keys. Other terminals need to emit a distinct
Ctrl+Shift+Z sequence (CSI-u/modifyOtherKeys); Ctrl+Y remains available otherwise.
The widget does not try to guess Shift state asynchronously.

The example has a real Windows console probe (synthetic native `INPUT_RECORD`s,
not just direct `OnEvent` calls), including Shift selection and Ctrl+C/V. It uses
an in-memory clipboard so it cannot replace the user's clipboard:

```powershell
.\build\editor-standalone\ptoolkit_editor_demo.exe --keyboard-probe
```

`ptoolkit_editor_mouse_probe.exe` exercises native mouse events through the
FTXUI event loop, including clicks on the first row, column placement, multiline
drag selection and clicking after scrolling. It uses the same title/divider/
filename offsets and pinned input layout as the host. This catches coordinate
normalization errors that direct `Editor::OnEvent()` tests cannot detect.

## Performance and scope

Edits affect only touched lines. Undo history stores inserted/deleted ranges,
not full-document snapshots. Each line has a version; glyph/highlight caches
are rebuilt only when that line changes. Rendering visits the visible viewport,
with a bounded neighborhood cache. Whole-document serialization is explicit.
Inserting/removing lines still shifts the following line descriptors; changing
a very long single line costs proportional to that line's length.

Tests cover randomized edit/undo/redo round trips, pairs, strings, indentation,
navigation, colors, focus and resizing. The reproducible workload contains
100,000 lines / 8.6 MB, 1,000 local edit+undo operations and 100 120x40 renders.
Timings printed by the test are CPU-side measurements, excluding terminal I/O.
Native system clipboard round-trip tests run in a separate noninteractive
window station. Windows gives each station its own clipboard, so these tests
neither inspect nor overwrite the interactive user's clipboard.

This is a basic code editor, not a complete VS Code replacement: it currently
has no search, folding, multiple cursors,
language server, or Chinese word segmentation. JSON highlighting is lexical;
the host performs JSON validation. The host also decides whether to soft-wrap;
this editor currently uses horizontal scrolling for long code lines.

## References and implementation choice

- [MarkdownFTXUI](https://github.com/zvasilev/MarkdownFTXUI): embeds in FTXUI,
  but its inspected editor uses Markdown-specific highlighting and does not
  provide the required undo/redo, auto-pairing and smart indentation interfaces.
- [Zep](https://github.com/Rezonality/zep): embeddable, but supplied rendering
  backends target Qt/ImGui rather than FTXUI.
- [Scinterm](https://orbitalquark.github.io/scinterm/): terminal Scintilla backed
  by curses, requiring another platform adapter to share an FTXUI screen.
- [VS Code word commands](https://github.com/microsoft/vscode/blob/main/src/vs/editor/contrib/wordOperations/browser/wordOperations.ts)
  and [word operations](https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/cursor/cursorWordOperations.ts):
  behavior reference, reimplemented with ASCII character classes.
- [VS Code Dark+](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_plus.json)
  and [Dark VS](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_vs.json):
  property `#9CDCFE`, string `#CE9178`, number `#B5CEA8`, boolean/null `#569CD6`,
  punctuation `#D4D4D4`.
