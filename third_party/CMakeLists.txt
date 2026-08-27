# Vendored header-only dependency, copied in (not a git submodule) — see
# moodycamel/VERSION for the exact upstream commit this was pinned from.
# Headers live at the repo root of the source tree (not under an include/
# subdir), matching upstream's actual layout.
#
# moodycamel readerwriterqueue, (c) Cameron Desrochers, simplified BSD.
# Upstream: https://github.com/cameron314/readerwriterqueue
# License text: third_party/moodycamel/LICENSE.md
add_library(naoto_moodycamel INTERFACE)
target_include_directories(naoto_moodycamel SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/moodycamel")