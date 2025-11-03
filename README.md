# A centralized logger for low-latency application

Logging is an important part of any application, whether it is logging general application behavior, warnings, errors, or even performance statistics.

Normal logging approaches would be output to terminal, or to be saved to files. The problems is the disk I/O is extremely slow and the whole string operations and formatting are slow too. Performing these operations on a critical path is a bad choice.

The idea of centralized logger is instead of writing information directly to the disk, the performance-sensitive threads will simply push the info into lock-free data queues and the rest will be handle by the logger thread.

Multiple threads can do submit to the logger as we called producers, and one will centralize logs from multiple sources by a consumer thread.

## Features

- Level: trace, info, debug, error, warning and fatal.
- Log to files: stderr, stdout, regular file.
- TODO: log over network.
- Batch buffer to reduce system call cost.

## How to integrate

```cpp
#include <logger>

// After program init.
logger::logger_options opts{.output_file = "stderr", .b};
logger::global_logger::init(opts);

// Do the log somewhere, from multiple threads, etc.
logger::global_logger::get().log(logger::severity::info, "hello %d from worker %p\n", i, (void*)pthread_self());

// Before program exit.
logger::global_logger::shutdown();
```

## Output log format

```text
<--date--> <-time-> <level> <log data--->
2025-11-03 16:25:08 INFO  - hello 3 from worker 0x787a2dffb6c0
```