file(GLOB_RECURSE ACCOUNT_SERVICE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(account_service ${ACCOUNT_SERVICE_SOURCES})

target_include_directories(account_service PRIVATE include)

target_link_libraries(account_service PRIVATE
    common
    naoto_dpdk
    naoto_absl
    EtcdCpp::EtcdCpp)