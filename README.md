# A centralized logger for low-latency application

Logging is an important part of any application, whether it is logging general application behavior, warnings, errors, or even performance statistics.

Normal logging approaches would be output to terminal, or to be saved to files. The problems is the disk I/O is extremely slow and the whole string operations and formatting are slow too. Performing these operations on a critical path is a bad choice.

The idea of centralized logger is instead of writing information directly to the disk, the performance-sensitive threads will simply push the info into lock-free data queues and the rest will be handle by the logger thread.

Multiple threads can do submit to the logger as we called producers, and one will centralize logs from multiple sources by a consumer thread.