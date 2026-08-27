#pragma once
//
// mpsc_queue.hpp
//
// Simple mutex-protected multi-producer / single-consumer queue.
//
// This is intentionally NOT a lock-free structure: every producer here
// (etcd watcher callback thread, multicast receiver threads, trading
// session threads) pushes at human/UI-observable rates (trades, book
// updates, confirmations) -- not the hot-path tick rate the rest of this
// system cares about. A short mutex hold per push/drain is irrelevant at
// this rate and far less likely to hide a bug than a hand-rolled
// lock-free ring buffer would be.
//
// Rule enforced by construction: ONLY the render/main thread ever calls
// drain_all() / try_pop(). Producer threads only ever call push(). This
// keeps all ImGui-visible state mutation on the single thread ImGui
// requires.
//

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

template <typename T>
class MpscQueue
{
public:
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    // Non-blocking: pulls everything currently queued. Call once per
    // frame from the render thread.
    std::vector<T> drain_all()
    {
        std::vector<T> out;
        std::lock_guard<std::mutex> lock(mutex_);
        out.reserve(queue_.size());
        while (!queue_.empty())
        {
            out.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        return out;
    }

    std::optional<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
            return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_; // available if a producer ever wants to
                                  // block-wait instead of poll; unused by
                                  // the render thread, which always polls
                                  // once per frame instead of blocking.
    std::deque<T> queue_;
};
