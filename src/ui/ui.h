#pragma once

#include <filesystem>
#include <functional>

#include <ftxui/component/component_base.hpp>

namespace ptoolkit
{
class App;
ftxui::Component CreateApplicationView(
    App& app, std::function<void()> on_exit,
    std::filesystem::path claude_settings_path = {},
    std::filesystem::path claude_presets_path = {});
}  // namespace ptoolkit
