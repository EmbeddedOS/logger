#pragma once
#include <array>
#include <atomic>

namespace lockfree
{
    template <typename T, std::size_t N>
    struct queue
    {
        queue() = default;
        queue(const queue &) = delete;
        queue(const queue &&) = delete;
        queue &operator=(const queue &) = delete;
        queue &operator=(const queue &&) = delete;

        bool push(const T& val)
        {

        }

        bool pop(T& val)
        {

        }

        size_t size() const noexcept {
            return _len.load();
        }

        std::atomic<size_t> _head;
        std::atomic<size_t> _tail;
        std::atomic<size_t> _len;

        std::array<T, N> _queue;
    };
}
