# market_server/account_service
#
# 1:1 translation of the existing Makefile — same pattern as
# matching_engine (see its CMakeLists.txt for the full flag-by-flag
# mapping), plus abseil: absl_flat_hash_set only (not flat_hash_map,
# per this service's Makefile — order_gateway needs both).

file(GLOB_RECURSE ACCOUNT_SERVICE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(account_service ${ACCOUNT_SERVICE_SOURCES})

target_include_directories(account_service PRIVATE include)

# Same shadowing caveat as matching_engine: if include/ still has its own
# copies of anything now living in common/, the local -Iinclude below
# shadows common's version silently. Remove duplicates once migrated.
target_link_libraries(account_service PRIVATE
    common
    naoto_dpdk
    naoto_absl
    EtcdCpp::EtcdCpp)