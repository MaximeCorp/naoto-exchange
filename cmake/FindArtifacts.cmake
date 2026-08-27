# cmake/FindArtifacts.cmake
#
# bootstrap/*.sh install each dependency into a hash-suffixed directory
# under artifacts/ (e.g. artifacts/dpdk-3fa9c1b2e4a1-x86_64-linux-gnu-gcc13-9c2f1a0b3d7e/),
# so a fixed path can't be hardcoded here — it changes whenever the
# submodule commit or the build options change. This locates the newest
# one that actually finished (has a .build-complete marker), so a stale
# or interrupted build is never picked up silently.
#
# Usage:
#   find_latest_artifact(dpdk DPDK_ARTIFACT_DIR)

function(find_latest_artifact name out_var)
    if(NOT DEFINED ARTIFACTS_DIR)
        message(FATAL_ERROR "ARTIFACTS_DIR is not set before calling find_latest_artifact().")
    endif()

    file(GLOB _candidates LIST_DIRECTORIES true "${ARTIFACTS_DIR}/${name}-*")

    set(_newest "")
    set(_newest_time -1)
    foreach(_cand ${_candidates})
        set(_marker "${_cand}/.build-complete")
        if(EXISTS "${_marker}")
            file(TIMESTAMP "${_marker}" _ts "%s")
            if(_ts GREATER _newest_time)
                set(_newest_time "${_ts}")
                set(_newest "${_cand}")
            endif()
        endif()
    endforeach()

    if(_newest STREQUAL "")
        message(FATAL_ERROR
            "No completed '${name}' artifact found under ${ARTIFACTS_DIR}. "
            "Run bootstrap/run-all.sh (or bootstrap/0*-*.sh individually) first.")
    endif()

    set(${out_var} "${_newest}" PARENT_SCOPE)
endfunction()