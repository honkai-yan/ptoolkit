cmake_minimum_required(VERSION 3.21)

project(ptoolkit VERSION 0.1.0 )

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED on)
set(CMAKE_CXX_STANDARD_REQUIRED on)

set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/ftxui EXCLUDE_FROM_ALL)
add_subdirectory(src/editor)

add_library(ptoolkit_app STATIC
  src/app.cc
  src/ui/command_input.cc
  src/claude_config.cc
  src/config_storage.cc
  src/ui/dialog.cc
  src/ui/claude_config_view.cc
  src/ui/ui.cc
  src/config.cc
  src/system_info.cc
  src/hardware_info.cc
)

target_include_directories(ptoolkit_app PUBLIC src)
target_include_directories(ptoolkit_app PRIVATE 3rdparty)
target_link_libraries(ptoolkit_app PUBLIC
  ptoolkit::editor
  ftxui::component ftxui::dom ftxui::screen
)

if(WIN32)
  target_link_libraries(ptoolkit_app PRIVATE dxgi gdi32)
endif()

add_executable(${CMAKE_PROJECT_NAME} src/main.cc)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ptoolkit_app)

option(PTOOLKIT_BUILD_TESTS "Build application UI tests" OFF)
if(PTOOLKIT_BUILD_TESTS)
  enable_testing()
  add_executable(ptoolkit_ui_tests tests/ui_tests.cc)
  target_link_libraries(ptoolkit_ui_tests PRIVATE ptoolkit_app)
  add_test(NAME ptoolkit_ui_tests COMMAND ptoolkit_ui_tests)
  if(WIN32)
    add_executable(ptoolkit_config_ui_probe tests/config_ui_probe.cc)
    target_link_libraries(ptoolkit_config_ui_probe PRIVATE ptoolkit_app)
  endif()
endif()
