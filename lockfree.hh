#pragma once
#include <array>
#include <atomic>

namespace lockfree
{
    const int cache_line_size = 64;
    using atm = std::atomic<size_t>;

    template <typename T, std::size_t N>
    struct queue
    { // Pure inheritance, no virtual method to reduce runtime cost.
        static_assert((N & (N - 1)) == 0, "N must be a power of two.");
        static_assert(sizeof(T) % cache_line_size == 0, "Must be cacheline padded.");
        static_assert(std::is_trivially_copyable<T>::value, "T should be trivially copyable.");

        queue() = default;
        queue(const queue &) = delete;
        queue(const queue &&) = delete;
        queue &operator=(const queue &) = delete;
        queue &operator=(const queue &&) = delete;

    protected:
        [[gnu::always_inline]] size_t calculate_idx(size_t c) const noexcept
        {
            return c & mask;
        }

        const size_t size = N;
        const size_t mask = N - 1;

        alignas(cache_line_size) atm _read_counter{0};
        char pad1[cache_line_size - sizeof(atm)];

        alignas(cache_line_size) std::atomic<size_t> _write_counter{0};
        char pad2[cache_line_size - sizeof(atm)];

        alignas(cache_line_size) std::array<T, N> _queue;
    };

    template <typename T, std::size_t N>
    struct mpsc_queue : queue<T, N>
    { // Mutiple producers - single consumer.
        using base = queue<T, N>;
        void push(const T &val)
        {
            auto idx = base::_write_counter.fetch_add(
                1, std::memory_order_relaxed);
            base::_queue[base::calculate_idx(idx)] = val;
        }

        bool try_pop(T &val)
        { // Since there's only one consumer, this function supposes to be run
          // only on one thread.
            auto rc = base::_read_counter.load(std::memory_order_relaxed);
            if (rc == base::_write_counter.load(std::memory_order_relaxed))
            {
                return false;
            }

            val = base::_queue[base::calculate_idx(rc)];
            rc++;

            base::_read_counter.store(rc, std::memory_order_release);
            return true;
        }
    };

    template <typename T, std::size_t N>
    struct mpmc_queue : queue<T, N>
    { // Multiple producers - mutiple consumers.
        using base = queue<T, N>;
        mpmc_queue() = default;
        mpmc_queue(const mpmc_queue &) = delete;
        mpmc_queue(const mpmc_queue &&) = delete;
        mpmc_queue &operator=(const mpmc_queue &) = delete;
        mpmc_queue &operator=(const mpmc_queue &&) = delete;

        void push(const T &val)
        {
            auto idx = base::_write_counter.fetch_add(
                1, std::memory_order_release);
            base::_queue[base::calculate_idx(idx)] = val;
        }

        bool try_pop(T &val)
        {
            for (;;)
            {
                auto rc = base::_read_counter.load(std::memory_order_relaxed);
                auto wc = base::_write_counter.load(std::memory_order_relaxed);
                if (rc == wc)
                {
                    return false;
                }

                auto next = (rc + 1);
                if (base::_read_counter.compare_exchange_weak(
                        rc, next,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                { // We do compare _read_counter with rc. If they are different,
                  // other consumer already updated it so we will loop again.
                    val = base::_queue[base::calculate_idx(next)];
                    return true;
                }
            }
        }
    };
}
