# Claude Code configuration editor

Run `build/ptoolkit.exe` and enter `claude_code_config_man` (or type `cla`,
then Tab and Enter). It reads the current user's `~/.claude/settings.json`,
validates that it is a JSON object, and opens an editor in the main content area.
On Windows `~` resolves through the Unicode `USERPROFILE`; elsewhere it uses
`HOME`. Missing/unreadable files and invalid JSON produce an error page.
Parser errors include a byte position, not a copy of configuration values.

The application title and its bottom separator remain visible. The command
input stays pinned at the bottom. The editor initially takes focus; **F6**
switches between it and the command input. Clicking either area also focuses it.
The editor's cursor and document remain intact while focus changes. Completion
only appears while the command input has focus.

Use Ctrl+Z / Ctrl+Shift+Z for undo/redo; arrows, Ctrl+arrows, Home/End and
Ctrl+Home/End move the cursor. Enter adds an intelligently indented line in the
editor, while Enter in the bottom input continues to execute commands.
Use Shift+movement keys, Ctrl+A, mouse dragging or Shift+click to select text.
Double-click selects a word; triple-click selects a whole line. Ctrl+D selects
the current word, then adds subsequent matching occurrences for simultaneous
editing. Ctrl+X cuts and Ctrl+C copies to the Windows system clipboard; Ctrl+V pastes text
and replaces a selection. Multiline paste is a single undo operation. Selection
is retained when focus changes.

While the editor has focus, Ctrl+C copies and never exits (with no selection it
does nothing). To exit, press F6 to focus the command input, then Ctrl+C.

Ctrl+S writes the buffer back to the originally opened `~/.claude/settings.json`.
It writes and flushes a temporary file in the same directory before replacing
the target. Failed writes leave the document modified and show an error dialog;
Enter closes it. Saving writes the exact buffer text, without JSON reformatting.
An asterisk marks modified content. Running `home`/`hello` changes the page;
running `claude_code_config_man` again re-reads the file and starts a fresh buffer.
The loader retains property order/spacing, normalizing CRLF to LF in memory.

## Presets

Ctrl+P toggles a floating sidebar on the left of the editor. It covers the
editor's full height without resizing or shifting the editor. Up/Down selects
the New preset button or a preset; the selected row uses `#63a8f8`.

- Enter on New preset asks for a name, then stores the current editor contents
  in `~/.ptoolkit/claude_code_presets/<name>.json`. Existing names are rejected.
- Enter on a preset loads its contents into the editor. Ctrl+S is required to
  write them back to Claude Code's settings. Loading can be undone in one step.
- `r` asks for a replacement name; existing names are rejected.
- `d` asks for confirmation before deleting the selected preset.

Name/confirmation dialogs use Enter to confirm and Escape to cancel, without
buttons. Error dialogs close with Enter. All dialogs share the custom `Dialog`
component, with background `#5484b9`, a white border and light text. Modal
events never reach the editor or command input.

Opening/using/closing the sidebar and its dialogs preserves all editor carets
and selections. Loading different text retains their positions where possible,
clamping positions outside the new buffer. Sidebar navigation does not move
the editor's cursor. New preset creation, rename and delete do not save settings.

See [the standalone editor documentation](../src/editor/README.md) for module
boundaries, performance, limitations, build instructions and dependency research.

## Verification

```powershell
cmake -S . -B build/verify -DPTOOLKIT_BUILD_TESTS=ON `
  -DPTOOLKIT_EDITOR_BUILD_TESTS=ON -DPTOOLKIT_EDITOR_BUILD_EXAMPLE=ON
cmake --build build/verify
ctest --test-dir build/verify --output-on-failure
.\build\verify\src\editor\ptoolkit_editor_demo.exe --keyboard-probe
.\build\verify\ptoolkit_config_ui_probe.exe
```

The configuration tests inject fixture paths under the build directory. They
cover successful loading, parse/range/type errors, missing files, BOM/CRLF,
indent detection, command completion, focus and undo/redo. Additional isolated
fixtures cover explicit saving, publication failures, preset creation/loading,
duplicate rejection, rename/delete cancellation, Unicode names, dialog colors,
and preservation of multiple editor selections during sidebar operations.
They do not write the user's real Claude configuration. Clipboard tests use a
private window station or an injected memory backend; the user's clipboard is
not inspected or overwritten.
