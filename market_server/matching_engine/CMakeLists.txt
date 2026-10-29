file(GLOB_RECURSE MATCHING_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(matching_engine ${MATCHING_ENGINE_SOURCES})

target_include_directories(matching_engine PRIVATE include)

target_link_libraries(matching_engine PRIVATE
    common
    naoto_dpdk
    EtcdCpp::EtcdCpp)