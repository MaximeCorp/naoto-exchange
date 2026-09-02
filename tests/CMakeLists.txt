# git_repo/tests
#
# Root of the repo's test tree, subdivided to mirror the top-level
# market_server/market_client split rather than implicitly claiming to
# cover both while only actually testing one. GoogleTest is declared
# once here since any subtree could end up depending on it.
#
# Included from the top-level CMakeLists.txt via `add_subdirectory
# (tests)`, so it must run *after* the market_server/* (and eventually
# market_client) add_subdirectory() calls it depends on for real
# library targets like `common`.

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(market_server)
add_subdirectory(market_client)
