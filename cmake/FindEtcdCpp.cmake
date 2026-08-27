# cmake/FindEtcdCpp.cmake
#
# etcd-cpp-apiv3 ships no CMake config package, so it's located directly.
# If EtcdCpp_ARTIFACT_DIR is set (root CMakeLists.txt sets it from the
# bootstrap-built artifact), that directory is searched first and
# exclusively for the library/headers — never silently falls back to a
# system copy that might be a different, incompatible version.

if(EtcdCpp_ARTIFACT_DIR)
    find_path(EtcdCpp_INCLUDE_DIR
        NAMES etcd/SyncClient.hpp
        PATHS "${EtcdCpp_ARTIFACT_DIR}/include"
        NO_DEFAULT_PATH)
    find_library(EtcdCpp_LIBRARY
        NAMES etcd-cpp-api
        PATHS "${EtcdCpp_ARTIFACT_DIR}/lib" "${EtcdCpp_ARTIFACT_DIR}/lib64"
        NO_DEFAULT_PATH)
else()
    find_path(EtcdCpp_INCLUDE_DIR NAMES etcd/SyncClient.hpp)
    find_library(EtcdCpp_LIBRARY  NAMES etcd-cpp-api)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EtcdCpp
    REQUIRED_VARS EtcdCpp_LIBRARY EtcdCpp_INCLUDE_DIR
    FAIL_MESSAGE
    "etcd-cpp-apiv3 not found. Run bootstrap/03-build-etcd-cpp.sh, or set EtcdCpp_ARTIFACT_DIR / install it system-wide.")

if(EtcdCpp_FOUND AND NOT TARGET EtcdCpp::EtcdCpp)
    # gRPC, protobuf, cpprestsdk, OpenSSL, zlib come from apt (step 01) —
    # etcd-cpp-apiv3 was built against exactly those, so they must resolve
    # from the same place, not from some other local prefix.
    find_package(Protobuf REQUIRED)
    find_package(OpenSSL  REQUIRED)
    find_package(ZLIB     REQUIRED)

    find_library(GRPCPP_LIBRARY  NAMES grpc++  REQUIRED)
    find_library(GRPC_LIBRARY    NAMES grpc    REQUIRED)
    find_library(CPPREST_LIBRARY NAMES cpprest REQUIRED)

    # GLOBAL: without it, this target's visibility is limited to the
    # directory that processed find_package(EtcdCpp) — which, chained
    # through a module include() from root, does not reliably reach
    # subdirectories added later via add_subdirectory(), even when the
    # find_package() call textually precedes them. This is what caused
    # "target EtcdCpp::EtcdCpp not found" in matching_engine's
    # target_link_libraries despite EtcdCpp_FOUND being true.
    add_library(EtcdCpp::EtcdCpp UNKNOWN IMPORTED GLOBAL)
    set_target_properties(EtcdCpp::EtcdCpp PROPERTIES
        IMPORTED_LOCATION             "${EtcdCpp_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EtcdCpp_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES
            "${GRPCPP_LIBRARY};${GRPC_LIBRARY};protobuf::libprotobuf;${CPPREST_LIBRARY};OpenSSL::SSL;OpenSSL::Crypto;ZLIB::ZLIB")
endif()

mark_as_advanced(EtcdCpp_INCLUDE_DIR EtcdCpp_LIBRARY)