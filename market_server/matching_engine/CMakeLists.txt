# market_server/matching_engine
#
# 1:1 translation of the existing Makefile:
#   -std=c++20 -Wall -Werror -Wextra -Wno-volatile  -> naoto_flags (root CMakeLists.txt)
#   -fsanitize=address (always on in the Makefile)  -> naoto_flags, gated by
#                                                       NAOTO_SANITIZE (default ON,
#                                                       matching current behavior —
#                                                       see the warning in the root
#                                                       CMakeLists.txt about flipping
#                                                       this off before timing anything)
#   pkg-config libdpdk (cflags + static libs)       -> naoto_dpdk
#   -letcd-cpp-api -lgrpc++ -lgrpc -lprotobuf
#     -lcpprest -lssl -lcrypto -lz -lpthread          -> EtcdCpp::EtcdCpp (+ naoto_flags
#                                                       for pthread)
#   -Iinclude                                        -> this target's own include/
#   find src -name '*.cpp'                           -> GLOB_RECURSE below

file(GLOB_RECURSE MATCHING_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(MarketExecution ${MATCHING_ENGINE_SOURCES})

target_include_directories(MarketExecution PRIVATE include)

# NOTE: also links `common` for the shared wire types (Order, etc.) now
# that they live in market_server/common/include. If matching_engine's own
# include/ still has its own copies of files also present in common/
# (Order.hpp and friends per the wire-type-drift issue from earlier), the
# local -Iinclude above will shadow common's version silently — remove the
# duplicates from matching_engine/include once the migration is done.
target_link_libraries(MarketExecution PRIVATE
    common
    naoto_dpdk
    EtcdCpp::EtcdCpp)