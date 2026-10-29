file(GLOB_RECURSE ORDER_GATEWAY_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(order_gateway ${ORDER_GATEWAY_SOURCES})

target_include_directories(order_gateway PRIVATE include)

target_link_libraries(order_gateway PRIVATE
    common
    naoto_dpdk
    naoto_absl
    EtcdCpp::EtcdCpp)