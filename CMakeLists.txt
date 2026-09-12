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

# Release by default. An -O0 build of this project is not representative
# of anything: the CRTP layers don't inline and the hot paths are ~10x off.
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
# matching_engine/Makefile currently always builds with ASan. Kept ON by
# default here so switching to CMake doesn't silently change behavior —
# flip this OFF (and see the ASan warning below) once you start timing
# anything, since redzones break the cache-line-isolation assumptions.
option(NAOTO_SANITIZE    "AddressSanitizer build (never benchmark this)"       ON)

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

# ---------------------------------------------------------------- DPDK
# Built by bootstrap/02-build-dpdk.sh into a hash-suffixed artifact dir.
# Meson installs a standard prefix layout, so it's found via pkg-config
# by pointing PKG_CONFIG_PATH at that prefix's pkgconfig dir(s) — never
# vendored, and never allowed to silently fall back to a system libdpdk.
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

# Static, not dynamic — matches the original Makefile's
# `pkg-config --static --libs libdpdk`. This is deliberate, not
# incidental: dynamic linking means the loader has to find every
# librte_*.so at runtime, and DPDK's libraries are layered (librte_ethdev
# depends on librte_kvargs, librte_log, librte_telemetry...) in a way
# that made RPATH propagation fragile across linkers/distros — see the
# chat history for the debugging session that motivated this. Static
# linking embeds all of it into the binary at build time, so there's
# nothing left to resolve at load time, and the RPATH/--disable-new-dtags
# handling below becomes a non-issue for DPDK specifically.
#
# pkg_check_modules populates <prefix>_STATIC_* variables automatically
# alongside the normal ones; DPDK's own .pc file already encodes the
# correct -Wl,--whole-archive / --no-whole-archive wrapping needed for
# PMD driver self-registration, so this is just consuming what DPDK's
# build already produces, not something we're constructing by hand.
add_library(naoto_dpdk INTERFACE)
target_include_directories(naoto_dpdk SYSTEM INTERFACE ${DPDK_STATIC_INCLUDE_DIRS})
target_compile_options(naoto_dpdk    INTERFACE ${DPDK_STATIC_CFLAGS_OTHER})
target_link_libraries(naoto_dpdk     INTERFACE ${DPDK_STATIC_LDFLAGS})

# etcd-cpp-api is still dynamic (see below), and its own transitive deps
# — grpc/protobuf/cpprest/ssl/crypto/zlib — all come from apt at standard
# system paths, already in the loader's default search path. So it's a
# single direct dependency with nothing layered underneath it at a
# nonstandard location, unlike DPDK was — RPATH is still set for it
# further down, but --disable-new-dtags is no longer load-bearing the
# way it was for DPDK. Left in place below since it's harmless and
# slightly more correct regardless.

# ---------------------------------------------------------------- etcd
# Built by bootstrap/03-build-etcd-cpp.sh. Point FindEtcdCpp.cmake at
# that exact artifact so it never silently picks up a different copy.
find_latest_artifact(etcd-cpp-apiv3 EtcdCpp_ARTIFACT_DIR)
message(STATUS "Using etcd-cpp-apiv3 artifact: ${EtcdCpp_ARTIFACT_DIR}")
find_package(EtcdCpp REQUIRED)

# libetcd-cpp-api.so itself still lives under the artifact prefix (it's
# not on apt, per the chat history), so it needs the same rpath treatment
# DPDK needed before switching to static. Its own dependencies (grpc,
# protobuf, cpprest, ssl, crypto, zlib) are all apt-installed at standard
# paths already in the loader's default search, so this one rpath entry
# is sufficient — no layered chain underneath it the way DPDK had.
target_link_options(EtcdCpp::EtcdCpp INTERFACE
    "-Wl,-rpath,${EtcdCpp_ARTIFACT_DIR}/lib"
    "-Wl,--disable-new-dtags")
    
# ---------------------------------------------------------------- abseil
# Used by order_gateway (flat_hash_map + flat_hash_set) and
# client_details_provider (flat_hash_set only) for client-state lookups —
# matching_engine doesn't use it, its own SIMD hash map is hand-written.
# One combined target covers both; client_details_provider just gets
# flat_hash_map's include path along for free, unused, harmless.
#
# Slated for removal later — both call sites are candidates to move onto
# the project's own SIMD hash map/set instead. See chat history: the
# client-id -> fd map may drop straight in as-is, the off-hot-path set
# may be a free byproduct if the map is generic over value type, or a
# small standalone structure otherwise. When that migration happens,
# remove this whole block, drop naoto_absl from both services below, and
# remove the apt package from bootstrap/01-install-deps.sh too.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ABSL REQUIRED IMPORTED_TARGET absl_flat_hash_map absl_flat_hash_set)
message(STATUS "abseil found via pkg-config")

add_library(naoto_absl INTERFACE)
target_link_libraries(naoto_absl INTERFACE PkgConfig::ABSL)

# ---------------------------------------------------------------- LTO
# Opt-in, NOT default-on. The link step is a single, non-parallel process
# that loads every object file's IR at once — and since DPDK itself was
# built with -flto (bootstrap/02-build-dpdk.sh), its static archive
# carries LTO bitcode too. Enabling this here can mean the link step
# tries to re-optimize the whole of DPDK's IR alongside common/'s heavy
# templated headers, in one process, which is a plausible explanation
# for a build suddenly needing much more RAM/CPU/time than the Makefile
# ever did — the Makefile had no LTO at all.
#
# Turn on with -DNAOTO_LTO=ON only when you actually want it (e.g. right
# before a benchmarking pass), not for everyday iteration.
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