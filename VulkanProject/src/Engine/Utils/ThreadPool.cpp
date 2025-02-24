#include "ThreadPool.h"
#include <iostream> // optional, for debug logs if needed

ThreadPool::ThreadPool(size_t threadCount)
{
    // If threadCount == 0, pick a default based on hardware concurrency
    if (threadCount == 0) {
        size_t hc = std::thread::hardware_concurrency();
        // Avoid zero or 1 threads if hardware_concurrency isn't giving a meaningful value:
        threadCount = (hc > 2) ? hc - 1 : 1;
    }

    // Launch worker threads
    m_workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; i++) {
        m_workers.emplace_back([this]() {
            workerThreadFunc();
            });
    }

    // Debug print (optional)
    // std::cout << "[ThreadPool] Started with " << threadCount << " workers.\n";
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::enqueueTask(const std::function<void()>& task)
{
    {
        std::unique_lock<std::mutex> lock(m_taskMutex);
        m_tasks.push(task);
    }
    // Notify one worker that there's a new task
    m_taskCondition.notify_one();
}

void ThreadPool::shutdown()
{
    // Mark that we're shutting down
    {
        std::unique_lock<std::mutex> lock(m_taskMutex);
        if (!m_isShuttingDown) {
            m_isShuttingDown = true;
            m_shutdownFlag.store(true, std::memory_order_relaxed);
        }
        else {
            // Already shutting down, no need to do it again
            return;
        }
    }

    // Wake up all worker threads so they can exit
    m_taskCondition.notify_all();

    // Join all worker threads
    for (auto& thread : m_workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_workers.clear();
}

void ThreadPool::workerThreadFunc()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);

            // Wait until we have tasks or we are shutting down
            m_taskCondition.wait(lock, [this]() {
                return !m_tasks.empty() || m_shutdownFlag.load(std::memory_order_relaxed);
                });

            // If shutting down and no tasks remain, break out
            if (m_shutdownFlag.load(std::memory_order_relaxed) && m_tasks.empty()) {
                break;
            }

            // Otherwise, pop a task if available
            if (!m_tasks.empty()) {
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            else {
                // If no task, keep waiting (handles spurious wake-ups)
                continue;
            }
        } // lock scope ends

        // Execute the task
        if (task) {
            task();
        }
    }
}

size_t ThreadPool::getThreadCount() const
{
    return m_workers.size();
}

size_t ThreadPool::getQueueSize()
{
    std::unique_lock<std::mutex> lock(m_taskMutex);
    return m_tasks.size();
}
