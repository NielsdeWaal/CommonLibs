#ifndef THREADPOOL_H
#define THREADPOOL_H

namespace Common {

class ThreadPoolJob
{
public:
    ThreadPoolJob() = default
    {}

};

class ThreadPool
{
public:
    ThreadPool(const std::size_t workerTreads)
        : mWorkers(workerThreads)
        , mNumberOfThreads(workerThreads)
    {}

private:
    std::vector<std::thread> mWorkers;
    const std::size_t mNumberOfThreads;

    std::queue<std::function<void()>> mJobs;
};

}

#endif // THREADPOOL_H
