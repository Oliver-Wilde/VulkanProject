#include "ThreadPool.h"
#include <iostream> // optional for logs

ThreadPool::ThreadPool(size_t threadCount,
    size_t maxMeshTasks,
    size_t maxGenTasks)
{
    if (threadCount == 0) {
        size_t hc = std::thread::hardware_concurrency();
        threadCount = (hc > 2) ? hc - 1 : 1;
    }

    // Store concurrency limits
    m_maxMeshing = maxMeshTasks;
    m_maxGeneration = maxGenTasks;

    // Launch worker threads
    m_workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; i++) {
        m_workers.emplace_back([this]() {
            workerThreadFunc();
            });
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::enqueueTask(const std::function<void()>& taskFunc,
    TaskType type,
    int priority)
{
    {
        std::unique_lock<std::mutex> lock(m_taskMutex);
        // Push the new Task into our priority queue
        m_tasks.push(Task(taskFunc, type, priority));
    }
    m_taskCondition.notify_one();
}

void ThreadPool::shutdown()
{
    {
        std::unique_lock<std::mutex> lock(m_taskMutex);
        if (!m_isShuttingDown) {
            m_isShuttingDown = true;
            m_shutdownFlag.store(true);
        }
        else {
            return; // already shutting down
        }
    }
    m_taskCondition.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
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

void ThreadPool::workerThreadFunc()
{
    while (true)
    {
        Task currentTask([]() {}, TaskType::Meshing, 0);
        bool foundTask = false;

        {
            std::unique_lock<std::mutex> lock(m_taskMutex);

            // Wait for tasks or shutdown
            m_taskCondition.wait(lock, [this]() {
                return !m_tasks.empty() || m_shutdownFlag.load();
                });

            if (m_shutdownFlag.load()) {
                // Shutting down + no tasks => exit
                if (m_tasks.empty()) {
                    break;
                }
            }

            // Try to find a task we can run that fits concurrency constraints
            // Because it's a priority queue, we'll check the top. If we can’t run it,
            // we might pop it and push it to a temporary container if it's not runnable.
            // Then we re-push if we skip it. This is a simple approach to searching.

            std::vector<Task> skippedTasks;
            while (!m_tasks.empty()) {
                Task topTask = m_tasks.top();
                bool canRun = false;

                if (topTask.type == TaskType::Meshing) {
                    if (m_activeMeshing.load() < m_maxMeshing) {
                        canRun = true;
                        m_activeMeshing.fetch_add(1);
                    }
                }
                else if (topTask.type == TaskType::Generation) {
                    if (m_activeGeneration.load() < m_maxGeneration) {
                        canRun = true;
                        m_activeGeneration.fetch_add(1);
                    }
                }

                if (canRun) {
                    // We will run this task
                    currentTask = topTask;
                    m_tasks.pop();
                    foundTask = true;
                    break;
                }
                else {
                    // Skip it for now => store it
                    skippedTasks.push_back(topTask);
                    m_tasks.pop();
                }
            }

            // Re-push skipped tasks so they remain in the queue
            for (auto& tsk : skippedTasks) {
                m_tasks.push(tsk);
            }

            // If we didn't find any runnable tasks, keep waiting
            if (!foundTask && !m_shutdownFlag.load()) {
                continue;
            }
        } // lock scope ends here

        // If we found a task, run it outside the lock
        if (foundTask) {
            // Execute the task function
            currentTask.func();

            // Decrement concurrency
            if (currentTask.type == TaskType::Meshing) {
                m_activeMeshing.fetch_sub(1);
            }
            else if (currentTask.type == TaskType::Generation) {
                m_activeGeneration.fetch_sub(1);
            }
        }
    } // end while(true)
}