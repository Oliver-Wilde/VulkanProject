#include "ThreadPool.h"
#include <iostream>   // optional logging

ThreadPool::ThreadPool(size_t threadCount,
    size_t maxMeshTasks,
    size_t maxGenTasks)
{
    if (threadCount == 0)
    {
        size_t hc = std::thread::hardware_concurrency();
        threadCount = (hc > 2) ? hc - 1 : 1;
    }

    m_maxMeshing = maxMeshTasks;
    m_maxGeneration = maxGenTasks;

    m_workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i)
        m_workers.emplace_back([this] { workerThreadFunc(); });
}

ThreadPool::~ThreadPool() { shutdown(); }

/* enqueue with priority ---------------------------------------------------- */
void ThreadPool::enqueueTask(const std::function<void()>& taskFunc,
    TaskType type, int priority)
{
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_tasks.emplace(taskFunc, type, priority);
    }
    m_taskCondition.notify_one();
}

/* orderly shutdown --------------------------------------------------------- */
void ThreadPool::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        if (m_isShuttingDown) return;
        m_isShuttingDown = true;
        m_shutdownFlag.store(true);
    }
    m_taskCondition.notify_all();

    for (auto& w : m_workers)
        if (w.joinable()) w.join();
    m_workers.clear();
}

size_t ThreadPool::getThreadCount() const { return m_workers.size(); }

/* **FIX** – protect queue size read with same mutex ----------------------- */
size_t ThreadPool::getQueueSize()
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    return m_tasks.size();
}

/* worker loop ------------------------------------------------------------- */
void ThreadPool::workerThreadFunc()
{
    while (true)
    {
        Task job([] {}, TaskType::Meshing, 0);
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lk(m_taskMutex);
            m_taskCondition.wait(lk, [&] {
                return !m_tasks.empty() || m_shutdownFlag.load();
                });

            if (m_shutdownFlag.load() && m_tasks.empty())
                return;

            /* find a runnable task within concurrency limits -------------- */
            std::vector<Task> skipped;
            while (!m_tasks.empty())
            {
                Task top = m_tasks.top();
                bool canRun = false;

                if (top.type == TaskType::Meshing &&
                    m_activeMeshing.load() < m_maxMeshing)
                {
                    canRun = true;
                    m_activeMeshing.fetch_add(1);
                }
                else if (top.type == TaskType::Generation &&
                    m_activeGeneration.load() < m_maxGeneration)
                {
                    canRun = true;
                    m_activeGeneration.fetch_add(1);
                }

                if (canRun)
                {
                    job = top;
                    m_tasks.pop();
                    haveJob = true;
                    break;
                }
                skipped.push_back(top);
                m_tasks.pop();
            }
            for (auto& s : skipped) m_tasks.push(s);

            if (!haveJob && !m_shutdownFlag.load())
                continue;
        } /* mutex released */

        if (haveJob)
        {
            job.func();   /* execute */

            if (job.type == TaskType::Meshing)
                m_activeMeshing.fetch_sub(1);
            else if (job.type == TaskType::Generation)
                m_activeGeneration.fetch_sub(1);
        }
    }
}
