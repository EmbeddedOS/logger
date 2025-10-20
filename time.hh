#pragma once
#include <unistd.h>

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