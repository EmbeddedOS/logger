#include <logger.hh>

struct Slot
{
    std::atomic<uint8_t> ready;
};

class LockFreeQueue
{
};
