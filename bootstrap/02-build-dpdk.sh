#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

SUB_PATH="${REPO_ROOT}/third_party/dpdk"
check_submodule "${SUB_PATH}" "meson.build"

TAG=$(compute_tag "${SUB_PATH}")

CORES=$(nproc)
AVAIL_MB=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
MB_PER_JOB=2048
JOBS_BY_MEM=$(( AVAIL_MB / MB_PER_JOB ))
JOBS=$(( JOBS_BY_MEM < CORES ? JOBS_BY_MEM : CORES ))
if (( JOBS < 1 )); then
    JOBS=1
fi
echo "==> Using ${JOBS} parallel jobs (cores: ${CORES}, available RAM: ${AVAIL_MB}MB)"

LINK_ARGS=""
if command -v ld.lld &>/dev/null; then
    echo "==> lld found, using -fuse-ld=lld"
    LINK_ARGS="-fuse-ld=lld"
else
    echo "==> lld not found (apt install lld to speed up the link step), using default linker"
fi

# Build the link-args string as ONE value, only appending LINK_ARGS if non-empty.
C_LINK_ARGS="-flto=${JOBS}"
if [[ -n "${LINK_ARGS}" ]]; then
    C_LINK_ARGS="${C_LINK_ARGS} ${LINK_ARGS}"
fi

# Options that affect the build artifact itself (excludes --prefix, which
# only affects install location). Hashed below so a changed flag here
# invalidates the cache instead of silently reusing a stale build.
#
# LTO note: -flto=${JOBS} (in c_args/c_link_args below) both enables LTO
# and pins its parallelism to the job count computed above. That's the
# only LTO switch here — meson's own -Db_lto=true was dropped because it
# duplicated this and could win the "last flag wins" race unpredictably.
CACHE_OPTS=(
    "--buildtype=release"
    "-Dexamples="
    "-Dtests=false"
    "-Ddisable_apps=*"
    "-Ddisable_drivers=crypto/*,event/*,baseband/*,compress/*,raw/*"
    "-Dc_args=-flto=${JOBS}"
    "-Dc_link_args=${C_LINK_ARGS}"
)

OPTS_HASH=$(printf '%s\n' "${CACHE_OPTS[@]}" | sha256sum | cut -d' ' -f1)
OUT=$(artifact_out_dir "dpdk" "${TAG}-${OPTS_HASH:0:12}")

if is_cached "${OUT}"; then
    echo "==> DPDK already built: ${OUT}"
    exit 0
fi

# Every -D value is fully quoted as a single string, including glob
# characters (*), so bash never expands them — meson receives the literal
# pattern and does its own glob matching internally.
MESON_OPTS=("--prefix=${OUT}" "${CACHE_OPTS[@]}")

BUILD_DIR="${SUB_PATH}/build"
echo "==> Configuring build (tag: ${TAG}, opts: ${OPTS_HASH:0:12})..."
rm -rf "${BUILD_DIR}"
mkdir -p "${OUT}"
meson setup "${BUILD_DIR}" "${SUB_PATH}" "${MESON_OPTS[@]}"

echo "==> Building DPDK..."
ninja -C "${BUILD_DIR}" -j"${JOBS}"
ninja -C "${BUILD_DIR}" install

mark_complete "${OUT}"
echo "==> 02-build-dpdk.sh complete: ${OUT}"