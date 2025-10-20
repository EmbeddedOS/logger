#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/uio.h>

#include <cstdarg>
#include <cstring>
#include <thread>
#include <string>
#include <vector>
#include <array>
#include <atomic>

#include <iostream>

#include <logger.hh>
#include <time.hh>

namespace logger
{
    const auto message_max_length = 512;
    const size_t queue_length = {1 << 15}; // Power of two.

    enum class severity : uint8_t
    {
        trace = 0,
        debug = 1,
        info = 2,
        warn = 3,
        error = 4,
        fatal = 5
    };

    inline const char *severity_str(severity lv)
    {
        switch (lv)
        {
        case severity::trace:
            return "TRACE";
        case severity::debug:
            return "DEBUG";
        case severity::info:
            return "INFO ";
        case severity::warn:
            return "WARN ";
        case severity::error:
            return "ERROR";
        case severity::fatal:
            return "FATAL";
        default:
            break;
        }
        return "NONE";
    }

    struct alignas(lockfree::cache_line_size) message
    {
        std::atomic_bool ready;
        uint8_t level;
        uint16_t len;
        timespec ts;
        char msg[message_max_length];
        char _pad[lockfree::cache_line_size -
                  (sizeof(std::atomic_bool) + 1 + 2 + sizeof(timespec) + message_max_length) % lockfree::cache_line_size];
    };

    struct logger_options
    {
        severity min_level{severity::trace};
        std::string output_file;
        size_t batch_write{512}; // Max message per write.
        // Adding more options here to expand the logger features, for example:
        // logging into remote server, config storage, flush, etc.
        // For now we only log to file.
    };

    class logger : lockfree::queue<message, queue_length>
    {
    public:
        logger(const logger_options &opt)
            : _opts{opt},
              _running{true}
        {
            init_consumer();
        }

        ~logger()
        {
            stop();
            _consumer.join();
            if (_fd > 2)
            {
                ::close(_fd);
            }
        }

        bool log(severity lv, const char *fmt, ...)
        {
            if (lv < _opts.min_level)
            {
                return false;
            }

            auto idx = base::_write_counter.fetch_add(
                1, std::memory_order_relaxed);
            message &slot = base::_queue[base::calculate_idx(idx)];

            if (slot.ready.load(std::memory_order_acquire))
            { // Full of ready slot.
                return false;
            }

            slot.level = static_cast<uint8_t>(lv);
            slot.ts = now_timespec();

            int len = 0;
            va_list ap;
            va_start(ap, fmt);
            len = vsnprintf(slot.msg, message_max_length, fmt, ap);
            va_end(ap);

            if (len < 0)
            {
                len = 0;
            }

            if (len >= static_cast<int>(message_max_length))
            {
                len = message_max_length - 1; // ensure space for eob.
                slot.msg[len] = '\0';
            }

            slot.len = static_cast<uint16_t>(len);
            slot.ready.store(true, std::memory_order_release);

            return true;
        }

        void stop() noexcept
        {
            _running.store(false, std::memory_order_relaxed);
        }

        void consume() noexcept
        {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            std::vector<iovec> vec;
            std::vector<std::array<char, 30>> hdrs;

            vec.reserve(_opts.batch_write);
            hdrs.reserve(_opts.batch_write);

            while (_running)
            {
                size_t drained = 0;
                auto rc = base::_read_counter.load(std::memory_order_relaxed);
                vec.clear();
                hdrs.clear();

                for (; drained < _opts.batch_write; drained++)
                { // Read all possible until max batch or no more to read.
                    message &m = base::_queue[base::calculate_idx(rc)];

                    if (!m.ready.load(std::memory_order_acquire))
                    { // No more ready slot.
                        break;
                    }

                    // Build header: timestamp + level + message.
                    hdrs.emplace_back();
                    auto &hdr = hdrs.back();
                    int off = 0;

                    off += fmt_ts_yyyy_mm_dd_hh_mm_ss(mess.ts, hdr.data());
                    hdr[off++] = ' ';
                    const char *lv = severity_str(static_cast<severity>(mess.level));
                    std::memcpy(hdr.data() + off, lv, 5);
                    off += 5;
                    hdr[off++] = ' ';
                    hdr[off++] = '-';
                    hdr[off++] = ' ';

                    vec.push_back({.iov_base = hdr.data(),
                                   .iov_len = static_cast<size_t>(off)});
                    vec.push_back({.iov_base = mess.msg,
                                   .iov_len = static_cast<size_t>(mess.len)});
                    std::cout << hdr.data() << ", len: " << mess.len << ", " << mess.msg;
                }

                if (!vec.empty())
                { // Log the buffer.
                    int res = writev_full(_fd, vec.data(), static_cast<int>(vec.size()));
                    if (vec.size() != res)
                    {
                        std::cerr << "err: " << res << " " << vec.size() << "\n";
                    }
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
        }

        void init_consumer()
        { // TODO expand more logger features here.
            if (_opts.output_file == "stdout")
            {
                _fd = STDOUT_FILENO;
            }
            else if (_opts.output_file == "stderr")
            {
                _fd = STDERR_FILENO;
            }
            else
            {
                _fd = ::open(_opts.output_file.c_str(),
                             O_CREAT | O_WRONLY | O_TRUNC | O_APPEND,
                             0644);
                assert(_fd > STDERR_FILENO);
            }

            _consumer = std::thread([this]()
                                    { this->consume(); });
        }

    private:
        std::atomic_bool _running;
        logger_options _opts;
        std::thread _consumer;
        int _fd;
    };

    class global_logger
    {
    public:
        static void init(const logger_options &opt = {})
        {
            instance() = new logger(opt);
        }

        static logger &get()
        {
            return *instance();
        }

        static void shutdown()
        {
            delete instance();
            instance() = nullptr;
        }

    private:
        static logger *&instance()
        {
            static logger *g{};
            return g;
        }
    };
}