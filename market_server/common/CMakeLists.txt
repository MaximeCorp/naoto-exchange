# market_server/common — wire types + generic infra shared by every service.
#
# Now has real .cpp sources (order.cpp, file_descriptors_ops.cpp — the
# non-hot-path pieces pulled out of their headers to cut compile time), so
# per the note that used to be here: add_library(... INTERFACE) has become
# add_library(... STATIC <the .cpp files>) and INTERFACE -> PUBLIC below.
# See the "Splitting into .cpp" note from earlier in the chat re: LTO —
# make sure LTO/IPO is on in release builds so the split doesn't cost real
# inlining at the callers that are still hot (VersionedFd, PriceLevel,
# OrderNode, etc. were deliberately left untouched/header-only for that
# reason and don't go in this list).

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(common STATIC ${COMMON_SOURCES})

target_include_directories(common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# epoll_server.ipp is included by epoll_server.hpp, not compiled directly —
# listed here only so it shows up in IDEs, never treated as a source file.
target_sources(common PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/epoll_server.ipp")

# moodycamel: consumer.hpp / storage_pool.hpp / unsafe_storage_pool.hpp
# are built on its SPSC queue.
# DPDK: udp_multicast_emitter.hpp sends over it directly.
# Both used to be INTERFACE deps of an INTERFACE lib; now common has actual
# object code too, but the headers still need these propagated to whatever
# links common, so they stay PUBLIC.
target_link_libraries(common PUBLIC
    naoto_flags
    naoto_moodycamel
    naoto_dpdk)
