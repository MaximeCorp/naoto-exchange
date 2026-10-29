file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(common STATIC ${COMMON_SOURCES})

target_include_directories(common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(common PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/epoll_server.ipp")

target_link_libraries(common PUBLIC
    naoto_flags
    naoto_moodycamel
    naoto_dpdk)
