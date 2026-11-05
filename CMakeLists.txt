cmake_minimum_required(VERSION 3.24)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()
# OPMAN_VERSION: the release tag without "v" (CI passes it); local builds are <newest tag>-dev
set(OPMAN_VERSION "" CACHE STRING "Version baked into the packages, e.g. 0.2.0 or 0.3.0-beta.1")
string(REGEX REPLACE "^v" "" OPMAN_VERSION "${OPMAN_VERSION}")  # a tag name works too
set(_opman_numeric 0.1.0)
execute_process(COMMAND git describe --tags --abbrev=0 --match "v[0-9]*" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE _opman_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_opman_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
  set(_opman_numeric ${CMAKE_MATCH_1})
endif()
if(OPMAN_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
  set(_opman_numeric ${CMAKE_MATCH_1})
endif()
project(opman VERSION ${_opman_numeric} LANGUAGES C CXX)
if(OPMAN_VERSION)
  set(OPMAN_VERSION_STRING ${OPMAN_VERSION})
else()
  set(OPMAN_VERSION_STRING ${PROJECT_VERSION}-dev)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)  # sources are UTF-8 (∑, √, ≤ ...)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.93.0-docking GIT_SHALLOW ON SOURCE_SUBDIR none)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5 SOURCE_SUBDIR none)
FetchContent_MakeAvailable(imgui json stb)

# ponytail: system glfw on Linux (nix), fetched elsewhere
find_package(glfw3 3.4 QUIET)
if(NOT glfw3_FOUND)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw GIT_TAG 3.4 GIT_SHALLOW ON)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)  # math italic letters live above U+FFFF

# core = everything but the window; shared by the app and the self-checks
add_library(opman_core STATIC src/doc.cpp src/edit.cpp src/math.cpp src/vi.cpp src/export.cpp)
target_include_directories(opman_core PUBLIC src)
target_link_libraries(opman_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(opman src/main.cpp src/layout.cpp)
target_link_libraries(opman PRIVATE opman_core imgui)
target_include_directories(opman PRIVATE ${stb_SOURCE_DIR})  # stb_image: pictures in notes

add_executable(opman_check tests/check.cpp)
target_link_libraries(opman_check PRIVATE opman_core)

# assets live next to the binary (macOS: in the .app's Resources); see resDir() in src/main.cpp
if(APPLE)
  set(OPMAN_RES_DIR $<TARGET_BUNDLE_CONTENT_DIR:opman>/Resources)
else()
  set(OPMAN_RES_DIR $<TARGET_FILE_DIR:opman>)
endif()
add_custom_command(TARGET opman POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${OPMAN_RES_DIR}/assets)

include(packaging/packaging.cmake)
