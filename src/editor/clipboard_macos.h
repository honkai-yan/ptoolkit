#pragma once

#include "ptoolkit_editor/clipboard.h"

namespace ptoolkit::editor::detail
{
// Empty name selects the general clipboard. Tests use a private named board.
std::shared_ptr<Clipboard> MakeMacClipboard(std::string_view name = {});
}
