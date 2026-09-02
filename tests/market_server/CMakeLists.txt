# git_repo/tests/market_server
#
# GoogleTest suite for the parts of market_server that are pure logic /
# in-memory data structures, plus EpollServer (real sockets, no DPDK/etcd
# needed) — see README.md for exactly what's covered and what isn't.
# Links against the real `common` target rather than vendoring its own
# copy of anything.

add_subdirectory(unit)
