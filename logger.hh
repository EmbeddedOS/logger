#pragma once
#include <stdint.h>

#include <lockfree.hh>

namespace log
{
    constexpr auto queue_length = 1000;

    enum class severity : uint8_t
    {
        trace = 0,
        debug = 1,
        info = 2,
        warn = 3,
        error = 4,
        fatal = 5
    };

    struct message
    {
    };

    class logger
    {
        auto consume() noexcept
        {
            struct message mess{};

            while (_running)
            {
                if (!_queue.pop(mess))
                {

                }
            }
        }

        auto stop() noexcept
        {
            _running.store(false);
        }

    private:
        lockfree::queue<message, queue_length> _queue;
        std::atomic_bool _running;
    };

}