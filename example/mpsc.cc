#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <logger.hh>

int main()
{
    logger::logger_options opts{.output_file = "stderr"};
    logger::global_logger::init(opts);
    {
        const int threads = 8;
        const int per_thread = 10000; // 10k each -> 80K lines.

        // Synchronization using condition variable (compatible with older GCC)
        std::mutex sync_mutex;
        std::condition_variable sync_cv;
        int ready_count = 0;
        bool start = false;

        std::vector<std::thread> v;
        v.reserve(threads);
        for (int t = 0; t < threads; ++t)
        {
            v.emplace_back([&]
                           {
            // Wait for all threads to be ready
            {
                std::unique_lock<std::mutex> lock(sync_mutex);
                ready_count++;
                if (ready_count == threads) {
                    start = true;
                    sync_cv.notify_all();
                } else {
                    sync_cv.wait(lock, [&]{ return start; });
                }
            }
            
            for (int i=0;i<per_thread;++i) {
                logger::global_logger::get().log(logger::severity::info, "hello %d from worker %p\n", i, (void*)pthread_self());
            } });
        }

        // Join all threads before they go out of scope
        for (auto &thread : v)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    logger::global_logger::shutdown();
}