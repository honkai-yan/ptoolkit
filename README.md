# ptoolkit

C++23 terminal toolkit with Windows and macOS backends. FTXUI 7.0.3 and
nlohmann/json are included in `3rdparty/`; no submodule setup is needed.

## macOS build and test

Requires CMake >= 3.21 and an Apple Clang/libc++ toolchain with C++23 support
(including `std::expected`, `std::format`, and `std::print`). Verified with
Xcode's Apple Clang 21 on macOS 27.0 / Apple Silicon. The macOS backends use
system frameworks and require no additional packages.

```sh
cmake -S . -B build \
  -DPTOOLKIT_BUILD_TESTS=ON \
  -DPTOOLKIT_EDITOR_BUILD_TESTS=ON \
  -DPTOOLKIT_EDITOR_BUILD_EXAMPLE=ON
cmake --build build -j 8
ctest --test-dir build --output-on-failure
./build/ptoolkit
```

Run native hardware and clipboard tests in a normal logged-in macOS session.
An execution sandbox that blocks DiskArbitration or the pasteboard service can
prevent these queries even when compilation and ordinary unit tests pass.
Clipboard tests use a private named pasteboard and preserve the general system
clipboard. UI/configuration tests use temporary fixtures.

Optional checks require Python 3 and do not open or modify user configuration:

```sh
python3 tests/macos_info_crosscheck.py build
python3 tests/terminal_smoke.py build
```

The first compares system/CPU/RAM/disk results with `sw_vers`, `sysctl`, and
`diskutil`, including a check against APFS capacity duplication. The second
uses a POSIX pseudo-terminal to check application page switching, editor
selection/copy/paste/undo/redo/navigation, exit, and terminal attribute recovery.
It injects VT key sequences; physical keyboard mappings, mouse gestures and IME
behavior still need checking in the chosen terminal emulator.

## Information interfaces

All results keep the `std::expected<value, std::string>` error convention.
Existing Windows version APIs remain available unchanged. New platform-neutral
entry points in `system_info` are `GetNameZh()`, `GetNameEn()`,
`GetDisplayVersion()`, and `GetBuildVersion()`. On macOS these return `macOS`
for both names, the product version (for example `27.0`), and the OS build
(for example `26A428`). The app uses these common entry points.

## Claude Code editors

`claude_code_config_man` edits `~/.claude/settings.json` and
`claude_code_prompt_man` edits the global `~/.claude/CLAUDE.md` prompt. Both
use the built-in editor with `Ctrl+S` to save and `Ctrl+P` for presets. See
[docs/claude-config.md](docs/claude-config.md) and
[docs/claude-prompt.md](docs/claude-prompt.md).

The five `hardware_info` functions and `GpuInfo` retain their names and types:

| Value | macOS source / meaning |
| --- | --- |
| CPU brand | `machdep.cpu.brand_string` |
| Physical cores | `hw.physicalcpu`, including performance and efficiency cores |
| GPUs | Metal devices; unified-memory GPUs have 0 dedicated VRAM and are shown as sharing system memory |
| RAM | `hw.memsize`, bytes |
| Disk capacity | Sum of whole internal physical disks, excluding external/removable disks and images; APFS containers/volumes are not counted separately |

Disk semantics differ slightly from Windows, which sums mounted fixed volumes
with drive letters: macOS reports physical capacity including partitions and
unallocated space. For discrete Metal GPUs the backend queries dedicated VRAM
from IOKit and returns an error if the driver does not expose it. Intel Macs,
discrete GPUs, external GPUs and multiple internal disks have not been tested
on physical machines in this change. Windows code paths are retained but have
not been rebuilt on Windows in this macOS session.

## Editor and FTXUI

See `src/editor/README.md` for embedding and key bindings. macOS uses a native
AppKit clipboard backend. Shortcuts remain Ctrl-based; Cmd+C/V and some modified
navigation keys can be intercepted by the terminal emulator. Ctrl+Y provides a
redo alternative to Ctrl+Shift+Z.

FTXUI stays a regular directory. Its existing Windows input patch is unchanged;
macOS uses the upstream POSIX terminal implementation. To run FTXUI's own tests
separately (first configuration downloads GoogleTest and Google Benchmark):

```sh
cmake -S 3rdparty/ftxui -B build/ftxui-tests \
  -DFTXUI_BUILD_TESTS=ON -DFTXUI_BUILD_EXAMPLES=OFF \
  -DFTXUI_BUILD_DOCS=OFF -DFTXUI_ENABLE_INSTALL=OFF
cmake --build build/ftxui-tests --target ftxui-tests -j 8
./build/ftxui-tests/ftxui-tests --gtest_brief=1
```

Validation on the machine above: all 5 project tests and all 352 FTXUI tests
passed, along with the system-command cross-check and the PTY interaction check.
