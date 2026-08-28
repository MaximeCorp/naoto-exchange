# market_server/order_gateway
#
# 1:1 translation of the existing Makefile — same pattern as
# matching_engine (see its CMakeLists.txt for the full flag-by-flag
# mapping), plus abseil: absl_flat_hash_map + absl_flat_hash_set.

file(GLOB_RECURSE ORDER_GATEWAY_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(order_gateway ${ORDER_GATEWAY_SOURCES})

target_include_directories(order_gateway PRIVATE include)

# Same shadowing caveat as matching_engine: if include/ still has its own
# copies of anything now living in common/, the local -Iinclude below
# shadows common's version silently. Remove duplicates once migrated.
target_link_libraries(order_gateway PRIVATE
    common
    naoto_dpdk
    naoto_absl
    EtcdCpp::EtcdCpp)