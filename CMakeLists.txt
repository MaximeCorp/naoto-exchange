cmake_minimum_required(VERSION 3.20)
project(MarketServer LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache not found - install it, rebuilds are much faster with it")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "No CMAKE_BUILD_TYPE given, defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -fno-omit-frame-pointer")

set(ARTIFACTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory bootstrap/*.sh installs DPDK and etcd-cpp-apiv3 into")

option(NAOTO_NATIVE_ARCH "Compile with -march=native"                          ON)
option(NAOTO_SANITIZE    "AddressSanitizer build (never benchmark this)"       OFF)

# ---------------------------------------------------------------- flags
add_library(naoto_flags INTERFACE)
target_compile_options(naoto_flags INTERFACE
    -Wall -Wextra -Werror -Wno-volatile)

if(NAOTO_NATIVE_ARCH)
    target_compile_options(naoto_flags INTERFACE -march=native)
else()
    target_compile_options(naoto_flags INTERFACE -msse4.2)
endif()

if(NAOTO_SANITIZE)
    message(WARNING
        "NAOTO_SANITIZE=ON: redzones change object layout, so cache-line "
        "isolation does not hold. Fine for correctness runs, never for timing.")
    target_compile_options(naoto_flags INTERFACE -fsanitize=address)
    target_link_options(naoto_flags    INTERFACE -fsanitize=address)
endif()

find_package(Threads REQUIRED)
target_link_libraries(naoto_flags INTERFACE Threads::Threads)

include(FindArtifacts)
find_latest_artifact(dpdk DPDK_ARTIFACT_DIR)
message(STATUS "Using DPDK artifact: ${DPDK_ARTIFACT_DIR}")

file(GLOB _dpdk_pc_dirs
    "${DPDK_ARTIFACT_DIR}/lib/pkgconfig"
    "${DPDK_ARTIFACT_DIR}/lib/*/pkgconfig"
    "${DPDK_ARTIFACT_DIR}/lib64/pkgconfig")
if(NOT _dpdk_pc_dirs)
    message(FATAL_ERROR "No pkgconfig dir found under ${DPDK_ARTIFACT_DIR}/lib*. "
                         "Did 02-build-dpdk.sh finish 'ninja install'?")
endif()
string(REPLACE ";" ":" _dpdk_pc_path "${_dpdk_pc_dirs}")
set(ENV{PKG_CONFIG_PATH} "${_dpdk_pc_path}:$ENV{PKG_CONFIG_PATH}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
message(STATUS "DPDK ${DPDK_VERSION}")

add_library(naoto_dpdk INTERFACE)
target_include_directories(naoto_dpdk SYSTEM INTERFACE ${DPDK_STATIC_INCLUDE_DIRS})
target_compile_options(naoto_dpdk    INTERFACE ${DPDK_STATIC_CFLAGS_OTHER})
target_link_libraries(naoto_dpdk     INTERFACE ${DPDK_STATIC_LDFLAGS})

find_latest_artifact(etcd-cpp-apiv3 EtcdCpp_ARTIFACT_DIR)
message(STATUS "Using etcd-cpp-apiv3 artifact: ${EtcdCpp_ARTIFACT_DIR}")
find_package(EtcdCpp REQUIRED)

target_link_options(EtcdCpp::EtcdCpp INTERFACE
    "-Wl,-rpath,${EtcdCpp_ARTIFACT_DIR}/lib"
    "-Wl,--disable-new-dtags")

find_package(PkgConfig REQUIRED)
pkg_check_modules(ABSL REQUIRED IMPORTED_TARGET absl_flat_hash_map absl_flat_hash_set)
message(STATUS "abseil found via pkg-config")

add_library(naoto_absl INTERFACE)
target_link_libraries(naoto_absl INTERFACE PkgConfig::ABSL)

option(NAOTO_LTO "Whole-program link-time optimization (slow, RAM-heavy link step)" OFF)
option(NAOTO_BUILD_TESTS "Build the GoogleTest suite under tests/" OFF)
option(NAOTO_PERF "Turn on performances measurement with timestamps" OFF)
option(NAOTO_SHARED_MEMORY "Three processes, no scaling, but shared memory on the hot path" OFF)

if(NAOTO_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NAOTO_IPO_OK)
    if(NAOTO_IPO_OK AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
else()
    target_link_options(naoto_dpdk INTERFACE -fno-lto)
endif()

if (NAOTO_PERF)
    add_compile_definitions(NAOTO_PERF)
endif()

if (NAOTO_SHARED_MEMORY)
    add_compile_definitions(NAOTO_SHARED_MEMORY)
endif()

add_subdirectory(third_party)
add_subdirectory(market_server/common)
add_subdirectory(market_server/matching_engine)
add_subdirectory(market_server/order_gateway)
add_subdirectory(market_server/account_service)
add_subdirectory(market_client)

if(NAOTO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()