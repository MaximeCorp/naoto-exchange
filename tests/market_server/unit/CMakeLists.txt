# order_gateway/include and account_service/include both define files
# named client_state.hpp, client_states.hpp, client_delta.hpp, and
# client_states_writer.hpp (two independent triple-buffer
# implementations, same filenames, different namespaces) — and each
# one's own header internally bare-includes its *own* sibling files by
# that same short name. Their real per-service builds never combine
# both directories on one include path, so this is purely an artifact
# of testing both flavors from one binary: no single global -I order
# can make bare `#include <client_state.hpp>` resolve correctly for
# both flavors in the same translation unit. Splitting into three
# OBJECT libraries with different include-dir priority order (rather
# than fighting -I ordering with source-file-level tricks) keeps this
# explicit and easy to reason about.

set(COMMON_TEST_LIBS GTest::gtest GTest::gtest_main common naoto_absl)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TESTS_OPENSSL REQUIRED IMPORTED_TARGET libcrypto)
list(APPEND COMMON_TEST_LIBS PkgConfig::TESTS_OPENSSL)

option(NAOTO_TESTS_UBSAN "Additionally build the test binary with UBSan" ON)

function(naoto_configure_test_objlib target)
    target_link_libraries(${target} PRIVATE ${COMMON_TEST_LIBS})
    if(NAOTO_TESTS_UBSAN)
        target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    endif()
endfunction()

# --- Group 1: no dependency on either client_state family (or only
# matching_engine's, which doesn't collide with anything) --------------
add_library(market_server_unit_tests_common OBJECT
    test_skip_list.cpp
    test_flat_hash_map.cpp
    test_storage_pools.cpp
    test_sequence_ring_buffer.cpp
    test_bytes_buffer.cpp
    test_object_batch_buffer.cpp
    test_order_wire_types.cpp
    test_price_level.cpp
    test_order_book.cpp
    test_bid_ask.cpp
    test_versioned_fd.cpp
    test_epoll_server.cpp
)
target_include_directories(market_server_unit_tests_common PRIVATE
    ${CMAKE_SOURCE_DIR}/market_server/matching_engine/include
    ${CMAKE_SOURCE_DIR}/market_server/common/include
)
naoto_configure_test_objlib(market_server_unit_tests_common)

# --- Group 2: needs order_gateway's client_state/client_states/
# client_delta/client_states_writer to win bare-include resolution ------
add_library(market_server_unit_tests_order_gateway OBJECT
    test_client_state.cpp
    test_client_states_triple_buffer.cpp
    test_client_states_writer_order_gateway.cpp
    test_order_risk_check.cpp
)
target_include_directories(market_server_unit_tests_order_gateway PRIVATE
    ${CMAKE_SOURCE_DIR}/market_server/order_gateway/include
    ${CMAKE_SOURCE_DIR}/market_server/common/include
    ${CMAKE_SOURCE_DIR}
)
naoto_configure_test_objlib(market_server_unit_tests_order_gateway)

# --- Group 3: needs account_service's versions to win instead -----------
add_library(market_server_unit_tests_account_service OBJECT
    test_client_state_account_service.cpp
    test_client_states_account_service.cpp
    test_client_states_writer_account_service.cpp
    test_client_request_processor.cpp
)
target_include_directories(market_server_unit_tests_account_service PRIVATE
    ${CMAKE_SOURCE_DIR}/market_server/account_service/include
    ${CMAKE_SOURCE_DIR}/market_server/common/include
    ${CMAKE_SOURCE_DIR}
)
target_sources(market_server_unit_tests_account_service PRIVATE
    ${CMAKE_SOURCE_DIR}/market_server/account_service/src/client_request_processor.cpp
)
naoto_configure_test_objlib(market_server_unit_tests_account_service)

# --- Final executable, linking the compiled objects from all three -----
add_executable(market_server_unit_tests
    $<TARGET_OBJECTS:market_server_unit_tests_common>
    $<TARGET_OBJECTS:market_server_unit_tests_order_gateway>
    $<TARGET_OBJECTS:market_server_unit_tests_account_service>
)
target_link_libraries(market_server_unit_tests PRIVATE ${COMMON_TEST_LIBS})
if(NAOTO_TESTS_UBSAN)
    target_link_options(market_server_unit_tests PRIVATE -fsanitize=undefined)
endif()

include(GoogleTest)
gtest_discover_tests(market_server_unit_tests)
