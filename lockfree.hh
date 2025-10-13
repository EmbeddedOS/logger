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

        void push(const T &val)
        {
            _queue[_head.load(std::memory_order_acquire)] = val;
            _head = (_head + 1) % size();
            _len.fetch_add(1, std::memory_order_relaxed);
        }

        bool pop(T &val)
        {
            if (_len == 0)
            {
                return false;
            }

            val = _queue[_tail.load(std::memory_order_release)];
            _tail = (_tail + 1) % size();
            _len.fetch_sub(1, std::memory_order_relaxed);

            return true;
        }

        size_t len() const noexcept
        {
            return _len.load(std::memory_order_relaxed);
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
