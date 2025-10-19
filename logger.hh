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

#include <iostream>

#include <lockfree.hh>

namespace logger
{
    const auto message_max_length = 512;
    const size_t queue_length = {1 << 16}; // Power of two.

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
        uint8_t level;
        uint16_t len;
        timespec ts;
        char msg[message_max_length];
        char _pad[lockfree::cache_line_size - (1 + 2 + sizeof(timespec) + message_max_length) % lockfree::cache_line_size];
    };

    inline timespec now_timespec() noexcept
    { // Monotonic wrapper for COARSE realtime (fast on Linux).
      // Fallback to REALTIME.
        timespec ts{};
#if defined(CLOCK_REALTIME_COARSE)
        if (clock_gettime(CLOCK_REALTIME_COARSE, &ts) == 0)
            return ts;
#endif
        clock_gettime(CLOCK_REALTIME, &ts);
        return ts;
    }

    inline int
    fmt_ts_yyyy_mm_dd_hh_mm_ss(const timespec &ts, char *dst) noexcept
    {
        time_t sec = ts.tv_sec;
        struct tm tm{};
        gmtime_r(&sec, &tm); // UTC.
        // YYYY-MM-DD HH:MM:SS (19 chars).
        int n = snprintf(dst, 20, "%04d-%02d-%02d %02d:%02d:%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
        return n; // should be 19.
    }

    static ssize_t writev_full(int fd, iovec *iov, int iovcnt) noexcept
    { // returns total bytes written, or -1 on unrecoverable error (errno preserved)
        ssize_t total_written = 0;
        while (iovcnt > 0)
        {
            ssize_t n = ::writev(fd, iov, iovcnt);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break; // caller handles retry/pending
                return -1; // unrecoverable
            }
            total_written += n;
            // consume fully-written iovecs
            int i = 0;
            while (i < iovcnt && n >= static_cast<ssize_t>(iov[i].iov_len))
            {
                n -= static_cast<ssize_t>(iov[i].iov_len);
                ++i;
            }
            if (i > 0)
            {
                int remaining = iovcnt - i;
                if (remaining > 0)
                    std::memmove(iov, iov + i, sizeof(iovec) * remaining);
                iovcnt = remaining;
            }
            if (n > 0 && iovcnt > 0)
            {
                // partial write into iov[0]
                iov[0].iov_base = static_cast<char *>(iov[0].iov_base) + n;
                iov[0].iov_len = static_cast<size_t>(iov[0].iov_len) - static_cast<size_t>(n);
            }
        }
        return total_written;
    }

    struct logger_options
    {
        severity min_level{severity::trace};
        std::string output_file;
        size_t batch_write{256}; // Max message per write.
        // Adding more options here to expand the logger features, for example:
        // logging into remote server, config storage, flush, etc.
        // For now we only log to file.
    };

    class logger
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

        void log(severity lv, const char *fmt, ...)
        {
            if (lv < _opts.min_level)
            {
                return;
            }

            int len = 0;
            struct message m = {.level = static_cast<uint8_t>(lv),
                                .ts = now_timespec()};

            va_list ap;
            va_start(ap, fmt);
            len = vsnprintf(m.msg, message_max_length, fmt, ap);
            va_end(ap);

            if (len < 0)
            {
                len = 0;
            }

            if (len >= static_cast<int>(message_max_length))
            {
                len = message_max_length - 1; // ensure space for eob.
                m.msg[len] = '\0';
            }

            m.len = static_cast<uint16_t>(len);
            _queue.push(m);
        }

        void stop() noexcept
        {
            _running.store(false, std::memory_order_relaxed);
        }

        void consume() noexcept
        {
            std::vector<iovec> vec;
            std::vector<std::array<char, lockfree::cache_line_size>> hdrs;

            vec.reserve(_opts.batch_write);
            hdrs.reserve(_opts.batch_write);

            struct message mess{};

            while (_running)
            {
                size_t drained = 0;
                vec.clear();
                hdrs.clear();

                for (; drained < _opts.batch_write; drained++)
                { // Read all possible until max batch or no more to read.
                    if (!_queue.try_pop(mess))
                    {
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
                    std::cout << hdr.data() << mess.msg;
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
        lockfree::mpsc_queue<message, queue_length> _queue;
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