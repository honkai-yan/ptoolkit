# Third-party dependencies

## FTXUI

- Upstream: https://github.com/ArthurSonzogni/FTXUI
- Stable tag: `v7.0.3` (upstream has no explicitly designated LTS branch/tag).
- Commit: `f921fad208912747c17d129a8ef75ec7624b6eec`
- License: MIT; see `ftxui/LICENSE`.
- Downloaded only one commit, without other branches or tag history:

```sh
git clone --depth 1 --single-branch --branch v7.0.3 --no-tags https://github.com/ArthurSonzogni/FTXUI.git 3rdparty/ftxui
```

CMake uses this local checkout; configuring/building requires no dependency
download. FTXUI examples, tests, documentation and install rules are disabled.

### Local Windows input patch

`patches/ftxui-7.0.3-native-input.patch` is applied to `ftxui`. It disables VT
input translation on Windows so native Ctrl+Shift+Z retains Shift, converts
navigation/function keys to FTXUI's existing sequences, and forwards native
mouse input. Existing FTXUI cleanup restores the original console mode.
Native Windows mouse coordinates are zero-based; the adapter converts them to
the VT parser's one-based coordinate convention before FTXUI subtracts the
screen origin. This avoids moving clicks one row up and one column left.
Processed console input is disabled while the loop runs so Ctrl+C is delivered
as a normal input event and can copy in a focused editor; ptoolkit's root handler
still exits on Ctrl+C when the command input has focus.
The patch adds `src/ftxui/component/win32_input.hpp` and edits `app.cpp`.
Other platforms retain the upstream input implementation.

To recreate the checkout after the shallow clone above:

```sh
git -C 3rdparty/ftxui apply ../patches/ftxui-7.0.3-native-input.patch
```

## nlohmann/json

- Upstream: https://github.com/nlohmann/json
- Pinned version: `v3.12.0`.
- Vendored files: `nlohmann/json.hpp` (upstream single-header distribution),
  `nlohmann/LICENSE.MIT`.
- Source: https://github.com/nlohmann/json/tree/v3.12.0/single_include/nlohmann
- License: MIT.
- Only the application configuration loader depends on this parser; the editor
  library has no dependency on it.
