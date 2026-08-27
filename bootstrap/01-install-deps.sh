#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# --- Platform check ----------------------------------------------------
if ! command -v apt-get &>/dev/null; then
    echo "Error: this script supports Ubuntu (apt) only." >&2
    echo "See README for supported platforms." >&2
    exit 1
fi

# etcd-cpp-apiv3 (built in step 03) links against whatever gRPC/protobuf
# apt resolves here. That pairing was only verified on Ubuntu 24.04
# (Noble); a different codename may resolve a gRPC etcd-cpp-apiv3 doesn't
# build cleanly against. Warn, don't block — it may well work elsewhere.
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    if [[ "${VERSION_CODENAME:-}" != "noble" ]]; then
        echo "Warning: tested on Ubuntu 24.04 (Noble). Detected: ${PRETTY_NAME:-unknown}." >&2
        echo "         gRPC/protobuf versions may differ enough to break the etcd-cpp-apiv3 build in step 03." >&2
    fi
else
    echo "Warning: /etc/os-release not found, cannot verify distro version." >&2
fi

# --- Pull submodules (scoped to these two only) -------------------------
echo "==> Fetching submodules..."
git -C "${REPO_ROOT}" submodule update --init --recursive -- \
    third_party/dpdk \
    third_party/etcd-cpp-apiv3

check_submodule "${REPO_ROOT}/third_party/dpdk" "meson.build"
check_submodule "${REPO_ROOT}/third_party/etcd-cpp-apiv3" "CMakeLists.txt"
echo "==> Submodules ready."

# --- Install apt packages -------------------------------------------------
echo "==> Installing apt packages..."
sudo apt-get update

PACKAGES=(
    build-essential
    meson
    ninja-build
    cmake
    pkg-config
    python3-pyelftools
    libboost-all-dev
    libssl-dev
    libgrpc-dev
    libgrpc++-dev
    libprotobuf-dev
    protobuf-compiler-grpc
    libcpprest-dev
)

sudo apt-get install -y "${PACKAGES[@]}"

# --- Record installed versions for reproducibility ------------------------
mkdir -p "${ARTIFACTS_DIR}"
dpkg -l "${PACKAGES[@]}" | tail -n +6 | awk '{print $2, $3}' > "${ARTIFACTS_DIR}/apt-deps.lock"
echo "==> Recorded apt package versions to ${ARTIFACTS_DIR}/apt-deps.lock"

echo "==> 01-install-deps.sh complete."