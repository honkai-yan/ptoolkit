cmake_minimum_required(VERSION 3.21)

project(ptoolkit VERSION 0.1.0 )

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED on)
set(CMAKE_CXX_STANDARD_REQUIRED on)

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

option(PTOOLKIT_BUILD_TESTS "Build application tests" OFF)
if(PTOOLKIT_BUILD_TESTS OR PTOOLKIT_EDITOR_BUILD_TESTS)
  enable_testing()
endif()

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
  src/ui/preset_editor_view.cc
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
  target_link_libraries(ptoolkit_app PRIVATE dxgi gdi32 advapi32)
elseif(APPLE)
  target_sources(ptoolkit_app PRIVATE src/hardware_info_macos.mm)
  set_source_files_properties(src/hardware_info_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(ptoolkit_app PRIVATE "-framework Foundation" "-framework Metal"
    "-framework IOKit" "-framework DiskArbitration")
endif()

add_executable(${CMAKE_PROJECT_NAME} src/main.cc)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ptoolkit_app)

if(PTOOLKIT_BUILD_TESTS)
  add_executable(ptoolkit_system_info_tests tests/system_info_tests.cc src/system_info.cc)
  target_include_directories(ptoolkit_system_info_tests PRIVATE src)
  add_test(NAME ptoolkit_system_info_tests COMMAND ptoolkit_system_info_tests)
  add_executable(ptoolkit_hardware_info_tests tests/hardware_info_tests.cc)
  target_link_libraries(ptoolkit_hardware_info_tests PRIVATE ptoolkit_app)
  add_test(NAME ptoolkit_hardware_info_tests COMMAND ptoolkit_hardware_info_tests)
  add_executable(ptoolkit_ui_tests tests/ui_tests.cc)
  target_link_libraries(ptoolkit_ui_tests PRIVATE ptoolkit_app)
  add_test(NAME ptoolkit_ui_tests COMMAND ptoolkit_ui_tests)
  if(WIN32)
    add_executable(ptoolkit_config_ui_probe tests/config_ui_probe.cc)
    target_link_libraries(ptoolkit_config_ui_probe PRIVATE ptoolkit_app)
  endif()
endif()
