# market_server/common — wire types + generic infra shared by every service.
# Header-only for now (src/ is empty), so this is an INTERFACE library.
# The moment a .cpp lands in src/, change add_library(... INTERFACE) to
# add_library(... STATIC <the .cpp files>) and INTERFACE -> PUBLIC below —
# see the "Splitting into .cpp" note from earlier in the chat re: LTO.

add_library(common INTERFACE)

target_include_directories(common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# epoll_server.ipp is included by epoll_server.hpp, not compiled directly —
# listed here only so it shows up in IDEs, never treated as a source file.
target_sources(common INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/epoll_server.ipp")

# moodycamel: consumer.hpp / storage_pool.hpp / unsafe_storage_pool.hpp
# are built on its SPSC queue.
# DPDK: udp_multicast_emitter.hpp sends over it directly.
# Both are INTERFACE deps of an INTERFACE lib, so they propagate to
# whatever links common — a service doesn't need to know common uses them.
target_link_libraries(common INTERFACE
    naoto_flags
    naoto_moodycamel
    naoto_dpdk)