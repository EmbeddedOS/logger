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

        queue()
        {
            // Initialize all sequence numbers.
            for (size_t i = 0; i < N; ++i)
            {
                _sequence[i].store(i, std::memory_order_relaxed);
            }
        }

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
        
        // Sequence numbers to track which slots are ready to read.
        std::array<std::atomic<size_t>, N> _sequence;
    };

    template <typename T, std::size_t N>
    struct mpsc_queue : queue<T, N>
    { // Mutiple producers - single consumer.
      // TODO: implement sequence numbers to track which slots are ready to read.
        using base = queue<T, N>;
        void push(const T &val)
        {
            // Reserve a slot
            auto idx = base::_write_counter.fetch_add(1, std::memory_order_relaxed);
            auto slot_idx = base::calculate_idx(idx);
            
            // Wait until the slot is available (in case the queue wrapped around)
            while (base::_sequence[slot_idx].load(std::memory_order_acquire) != idx)
            {
                // Spin wait - slot not ready yet
            }
            
            // Write data to the slot
            base::_queue[slot_idx] = val;
            
            // Mark slot as ready for consumer (increment sequence to next expected value)
            base::_sequence[slot_idx].store(idx + 1, std::memory_order_release);
        }

        bool try_pop(T &val)
        { // Since there's only one consumer, this function supposes to be run
          // only on one thread.
            auto rc = base::_read_counter.load(std::memory_order_relaxed);
            auto slot_idx = base::calculate_idx(rc);
            
            // Check if the slot is ready to read (sequence should be rc + 1)
            if (base::_sequence[slot_idx].load(std::memory_order_acquire) != rc + 1)
            {
                return false; // Data not ready yet
            }

            // Read the data
            val = base::_queue[slot_idx];
            
            // Mark slot as available for reuse (set sequence to next cycle)
            base::_sequence[slot_idx].store(rc + base::size, std::memory_order_release);
            
            // Increment read counter
            base::_read_counter.store(rc + 1, std::memory_order_release);
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
