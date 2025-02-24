#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

/**
 * A simple thread pool that runs std::function<void()> tasks on worker threads.
 * Usage:
 * 1) Construct ThreadPool with desired # of threads.
 * 2) Call enqueueTask(...) with a lambda or function to run in background.
 * 3) When done, destruct the pool or explicitly shut it down.
 */
class ThreadPool
{
public:
    /**
     * Creates a pool with 'threadCount' worker threads.
     * If threadCount == 0, it defaults to using hardware_concurrency - 1 (if >0).
     */
    explicit ThreadPool(size_t threadCount = 0);

    /**
     * Destructor. Waits for all tasks to finish, then joins all threads.
     */
    ~ThreadPool();

    /**
     * Enqueues a new task to run asynchronously in one of the worker threads.
     * @param task A callable taking no arguments and returning void.
     */
    void enqueueTask(const std::function<void()>& task);

    /**
     * Shuts down the pool. Waits for all currently enqueued tasks,
     * then joins worker threads. Safe to call multiple times.
     */
    void shutdown();

    /**
     * @return How many worker threads this pool has.
     * (Safe to keep const; it doesn’t lock anything.)
     */
    size_t getThreadCount() const;

    /**
     * @return The current number of tasks waiting in the queue.
     * Not const, because we lock a mutex inside.
     */
    size_t getQueueSize();

private:
    /**
     * Worker thread loop that pops tasks from the queue.
     */
    void workerThreadFunc();

private:
    std::vector<std::thread>            m_workers;
    std::queue<std::function<void()>>   m_tasks;

    std::mutex                          m_taskMutex;
    std::condition_variable             m_taskCondition;
    std::atomic<bool>                   m_shutdownFlag{ false };  ///< signals workers to stop
    bool                                m_isShuttingDown = false;
};
