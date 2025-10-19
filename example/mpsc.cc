#include <thread>
#include <vector>
#include <barrier>

#include <logger.hh>

int main()
{
    logger::logger_options opts{.output_file = "./test.txt"};
    logger::global_logger::init(opts);
    for (int i = 0; i < 100; ++i)
    {
        logger::global_logger::get().log(logger::severity::info, "hello %d from worker %p\n", i, (void *)pthread_self());
    };
    {
        // const int threads = 1;
        // const int per_thread = 10000; // 20k each -> 80K lines.
        // std::barrier sync(threads);
        // std::vector<std::jthread> v;
        // v.reserve(threads);
        // for (int t = 0; t < threads; ++t)
        // {
        //     v.emplace_back([&]
        //                    {
        //     sync.arrive_and_wait();
        //     for (int i=0;i<per_thread;++i) {
        //         logger::global_logger::get().log(logger::severity::info, "hello %d from worker %p\n", i, (void*)pthread_self());
        //     } });
        // }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    logger::global_logger::shutdown();
}