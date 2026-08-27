#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

SUB_PATH="${REPO_ROOT}/third_party/etcd-cpp-apiv3"
check_submodule "${SUB_PATH}" "CMakeLists.txt"

TAG=$(compute_tag "${SUB_PATH}")
OUT=$(artifact_out_dir "etcd-cpp-apiv3" "${TAG}")

if is_cached "${OUT}"; then
    echo "==> etcd-cpp-apiv3 already built: ${OUT}"
    exit 0
fi

echo "==> Building etcd-cpp-apiv3 (tag: ${TAG})..."
BUILD_DIR="${SUB_PATH}/build"
rm -rf "${BUILD_DIR}"
mkdir -p "${OUT}"

# boost, openssl, protobuf, gRPC, cpprestsdk come from apt — found via
# standard system paths, no CMAKE_PREFIX_PATH needed for those.
cmake -S "${SUB_PATH}" -B "${BUILD_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${OUT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ETCD_CORE_ONLY=OFF

cmake --build "${BUILD_DIR}" -j"$(nproc)"
cmake --install "${BUILD_DIR}"

mark_complete "${OUT}"
echo "==> 03-build-etcd-cpp.sh complete: ${OUT}"