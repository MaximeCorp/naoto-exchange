#pragma once

#include <cstddef>
#include <optional>

// Forward declaration of the real POSIX ::rlimit (defined in
// <sys/resource.h>, included only by file_descriptors_ops.cpp). Declaring
// it here at global scope — before entering namespace naoto — means the
// unqualified `struct rlimit *` below binds to this same global tag
// instead of accidentally declaring a new, unrelated naoto::rlimit.
struct rlimit;

namespace naoto
{
    // Not thread safe
    //
    // Startup/one-time rlimit setup, never touched on the hot path - all
    // methods are defined out of line in file_descriptors_ops.cpp so the
    // <sys/resource.h>/<cstring>/<iostream> implementation details don't
    // get parsed by every translation unit that just wants getMaxFd().
    class FileDescriptorsOps
    {
    private:
        static inline std::optional<size_t> MaxFd = std::nullopt;

        // Note: qualified as ::rlimit (global scope) rather than plain
        // `rlimit` — without <sys/resource.h> visible here, an
        // unqualified `struct rlimit` in this namespace would declare a
        // brand new (incomplete, wrong) naoto::rlimit type instead of
        // referring to the real POSIX one from <sys/resource.h>.
        [[nodiscard]] static bool getRLimit(struct ::rlimit *rl);

        [[nodiscard]] static size_t findMaxFd(void) noexcept;

    public:
        static void setMaxFd(const size_t nb_fds);

        static void capMaxFd(void);

        [[nodiscard]] static size_t getMaxFd(void) noexcept;
    };
} // namespace naoto
