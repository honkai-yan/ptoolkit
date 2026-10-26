# Claude Code global prompt editor

Run `build/ptoolkit` and enter `claude_code_prompt_man` (or type `cla`, then
Down and Tab to complete). The page reads the current user's
`~/.claude/CLAUDE.md` and opens it in the same editor used by the settings
page. On Windows `~` resolves through the Unicode `USERPROFILE`; elsewhere it
uses `HOME`.

The file is plain text and is not JSON-validated. Markdown prose, quotes and
brackets are rendered without JSON token coloring; editing, selection,
undo/redo, clipboard and navigation keys are otherwise identical to
[the settings editor](claude-config.md).

A missing file opens an empty buffer so the prompt can be created from scratch.
`Ctrl+S` writes the exact buffer to `~/.claude/CLAUDE.md`, creating the
directory if necessary. Existing files are updated the same way as settings:
a temporary file in the same directory is flushed and published by rename. An
asterisk marks unsaved changes; a failed write leaves the buffer modified and
shows an error dialog. Running the command again re-reads the file and starts
a fresh buffer.

**F6** switches focus between the editor and the command input. While the
editor is focused **Ctrl+C** copies instead of exiting; focus the command input
before using Ctrl+C to leave the application.

## Presets

`Ctrl+P` toggles the same sidebar used by `claude_code_config_man`: New preset,
Enter to load, `r` to rename, `d` to delete, Escape to cancel a prompt. Presets
store the current editor contents in
`~/.ptoolkit/claude_code_prompt_presets/<name>.md`. Loading a preset only
replaces the editor buffer; press `Ctrl+S` to write it to Claude Code's global
prompt. Prompt presets are separate from JSON settings presets and are filtered
to the `.md` extension.

## Verification

The UI tests inject temporary prompt and preset paths under the build
directory. They cover plain-text loading, missing-file creation, saving to the
original target, plain rendering, and the complete preset lifecycle. The PTY
smoke test runs the application with an isolated `HOME`:

```sh
python3 tests/terminal_smoke.py build
```

No test reads or writes the user's real `~/.claude/CLAUDE.md`.
