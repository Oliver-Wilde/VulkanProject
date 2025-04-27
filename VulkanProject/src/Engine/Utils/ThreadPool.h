// ============================================================================
// ThreadPool.h – work‑stealing pool with runtime meshing‑limit control
// ============================================================================
#pragma once
#include <functional>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

// ─── task metadata ─────────────────────────────────────────────────────────
enum class TaskType : uint8_t { Meshing, Generation };

struct Task
{
    std::function<void()> func;
    TaskType              type;
    int                   priority;
};

struct alignas(64) WorkDeque
{
    std::deque<Task> q;
    std::mutex       mtx;
};

class ThreadPool
{
public:
    ThreadPool(size_t threadCount = 0,
        size_t maxMeshTasks = 4,
        size_t maxGenTasks = 4);
    ~ThreadPool();

    void enqueueTask(const std::function<void()>& fn,
        TaskType type,
        int priority = 0);

    void shutdown();

    // runtime introspection --------------------------------------------------
    size_t getThreadCount() const { return m_workers.size(); }
    size_t getQueueSize();

    // NEW: runtime control of meshing concurrency ----------------------------
    size_t getMaxMeshing() const;
    void   setMaxMeshing(size_t newLimit);

private:
    void workerMain(size_t index);
    bool tryPopLocal(size_t idx, Task& out);
    bool trySteal(size_t thiefIdx, Task& out);

    std::vector<std::unique_ptr<WorkDeque>> m_queues;
    std::vector<std::thread> m_workers;

    std::atomic<bool>   m_shutdown{ false };
    std::atomic<size_t> m_rrEnq{ 0 };

    // limits (meshing may change at runtime) ---------------------------------
    std::atomic<size_t> m_maxMeshing;
    const size_t        m_maxGeneration;

    std::atomic<size_t> m_activeMeshing{ 0 };
    std::atomic<size_t> m_activeGeneration{ 0 };

    std::condition_variable m_cv;
    std::mutex              m_sleepMtx;
};
