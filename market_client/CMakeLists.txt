cmake_minimum_required(VERSION 3.16)
project(trading_dashboard CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# etcd-cpp-apiv3 and its dependencies. Assumed already installed system-
# wide on this project's build machines (as noted in the project context
# doc). Adjust find_library paths if they live somewhere non-standard.
find_library(ETCD_CPP_API_LIB etcd-cpp-api REQUIRED)
find_library(GRPCPP_LIB grpc++ REQUIRED)
find_library(GRPC_LIB grpc REQUIRED)
find_library(PROTOBUF_LIB protobuf REQUIRED)
find_library(CPPREST_LIB cpprest REQUIRED)

include(FetchContent)

# If your connection struggles with `cmake ..` doing the git clone itself
# (slow/flaky network), clone these yourself with more visibility/retry
# control and point CMake at the local copy instead:
#   git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git /tmp/deps/imgui
#   git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git /tmp/deps/json
#   cmake -DLOCAL_DEPS_DIR=/tmp/deps ..
set(LOCAL_DEPS_DIR "" CACHE PATH "If set, use pre-cloned imgui/json from here instead of fetching")

if(LOCAL_DEPS_DIR)
  FetchContent_Declare(imgui SOURCE_DIR "${LOCAL_DEPS_DIR}/imgui")
  FetchContent_Declare(json SOURCE_DIR "${LOCAL_DEPS_DIR}/json")
else()
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
endif()

FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(json)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${GLFW_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC ${GLFW_LIBRARIES} OpenGL::GL)

add_executable(trading_dashboard
  src/main.cpp
)

target_include_directories(trading_dashboard PRIVATE
  include
  ${GLFW_INCLUDE_DIRS}
)

target_link_libraries(trading_dashboard PRIVATE
  imgui
  ${GLFW_LIBRARIES}
  OpenGL::GL
  ${ETCD_CPP_API_LIB}
  ${GRPCPP_LIB}
  ${GRPC_LIB}
  ${PROTOBUF_LIB}
  ${CPPREST_LIB}
  nlohmann_json::nlohmann_json
  pthread
)

target_compile_options(trading_dashboard PRIVATE -Wall -Wextra)

# Ship the fonts bundled with ImGui (already fetched above) next to the
# built executable, so main.cpp can load a real typeface instead of
# ImGui's default pixel font -- checked at runtime with a graceful
# fallback if this copy step is ever skipped/missing (see main.cpp).
#
# Two copy mechanisms, deliberately redundant:
#   1. POST_BUILD -- runs whenever the target actually rebuilds/relinks.
#   2. Configure-time file(COPY) below -- runs on every `cmake ..`
#      invocation UNCONDITIONALLY, regardless of whether CMake considers
#      the target itself up to date. (1) alone can silently do nothing
#      if the target isn't rebuilt (e.g. after a partial `clean`, or if
#      build/fonts/ was deleted separately without touching any source
#      file) -- CMake ties POST_BUILD to the target's own rebuild
#      trigger, not to "does this file currently exist." (2) closes that
#      gap: if fonts/ is ever missing, just rerunning `cmake ..` (no
#      source changes needed) fixes it.
add_custom_command(TARGET trading_dashboard POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${imgui_SOURCE_DIR}/misc/fonts"
          "$<TARGET_FILE_DIR:trading_dashboard>/fonts"
  COMMENT "Copying ImGui bundled fonts next to the executable"
)
file(COPY "${imgui_SOURCE_DIR}/misc/fonts/" DESTINATION "${CMAKE_BINARY_DIR}/fonts")