include(${CMAKE_CURRENT_LIST_DIR}/clang.cmake)

if(DEFINED ENV{VCPKG_ROOT})
  include($ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
else()
  message(FATAL_ERROR "$VCPKG_ROOT Not Found")
endif()
