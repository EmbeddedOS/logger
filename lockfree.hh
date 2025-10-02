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

        bool push(const T &val)
        {
            _queue[_head];
            _head = (_head + 1) % size();
            _len++;
        }

        bool pop(T &val)
        {
            val = _queue[_tail];
            _tail = (_tail + 1) % size();
            _len--;
        }

        size_t len() const noexcept
        {
            return _len.load();
        }

        constexpr size_t size()
        {
            return N;
        }

        std::atomic<size_t> _head;
        std::atomic<size_t> _tail;
        std::atomic<size_t> _len;

        std::array<T, N> _queue;
    };
}
