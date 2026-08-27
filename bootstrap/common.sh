#!/usr/bin/env bash
# bootstrap/common.sh — shared helpers, sourced by numbered steps.
# Not meant to be executed directly.

REPO_ROOT="$(git rev-parse --show-toplevel)"
ARTIFACTS_DIR="${REPO_ROOT}/artifacts"

# compute_tag <submodule_path>
# Prints a cache key: <commit>-<triplet>-gcc<version>
compute_tag() {
    local sub_path="$1"
    local commit triplet cc_version
    commit=$(git -C "${sub_path}" rev-parse HEAD)
    triplet="$(uname -m)-$(cc -dumpmachine 2>/dev/null || echo unknown)"
    cc_version=$(cc -dumpversion)
    echo "${commit:0:12}-${triplet}-gcc${cc_version}"
}

# artifact_out_dir <name> <tag>
artifact_out_dir() {
    local name="$1" tag="$2"
    echo "${ARTIFACTS_DIR}/${name}-${tag}"
}

# is_cached <out_dir>
is_cached() {
    [[ -f "$1/.build-complete" ]]
}

# mark_complete <out_dir>
mark_complete() {
    date -u +"%Y-%m-%dT%H:%M:%SZ" > "$1/.build-complete"
}

# check_submodule <submodule_path> [marker_file]
# Fails loudly if the submodule wasn't initialized / checked out properly.
check_submodule() {
    local sub_path="$1" marker="${2:-}"
    if [[ -n "${marker}" ]]; then
        if [[ ! -f "${sub_path}/${marker}" ]]; then
            echo "Error: ${sub_path} missing expected file '${marker}'." >&2
            echo "Try: git -C '${REPO_ROOT}' submodule update --init --recursive -- '${sub_path#${REPO_ROOT}/}'" >&2
            exit 1
        fi
    elif [[ -z "$(ls -A "${sub_path}" 2>/dev/null)" ]]; then
        echo "Error: ${sub_path} is empty." >&2
        exit 1
    fi
}