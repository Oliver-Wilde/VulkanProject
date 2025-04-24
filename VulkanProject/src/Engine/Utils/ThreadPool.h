#pragma once
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>

// For example, define your task types:
enum class TaskType
{
    Meshing,
    Generation,
    // ... Add more if needed
};

struct Task
{
    std::function<void()>  func;
    TaskType               type;
    int                    priority;

    // Constructor helper
    Task(const std::function<void()>& f, TaskType t, int p)
        : func(f), type(t), priority(p) {}
};

// Comparator for priority_queue
// Higher priority = tasks should come out first
struct TaskCompare
{
    // Return true if lhs < rhs => means "rhs is higher priority"
    bool operator()(const Task& lhs, const Task& rhs)
    {
        return lhs.priority < rhs.priority;
    }
};

class ThreadPool
{
public:
    // You can pass concurrency limits into the constructor
    ThreadPool(size_t totalThreads = 0,
        size_t maxMeshTasks = 4,
        size_t maxGenTasks = 4);
    ~ThreadPool();

    // Instead of just enqueueTask(...), we have a version that includes priority & type
    void enqueueTask(const std::function<void()>& taskFunc,
        TaskType type,
        int priority = 0);

    void shutdown();
    size_t getThreadCount() const;
    size_t getQueueSize(); // For debugging

private:
    // Worker thread function
    void workerThreadFunc();

    // We use a priority queue that compares Task priorities
    std::priority_queue<Task, std::vector<Task>, TaskCompare> m_tasks;

    // Concurrency counters
    size_t                 m_maxMeshing = 2;
    size_t                 m_maxGeneration = 2;
    std::atomic<size_t>    m_activeMeshing{ 0 };
    std::atomic<size_t>    m_activeGeneration{ 0 };

    // Other existing members
    std::vector<std::thread> m_workers;
    std::mutex               m_taskMutex;
    std::condition_variable  m_taskCondition;
    bool                     m_isShuttingDown = false;
    std::atomic<bool>        m_shutdownFlag{ false };
};
