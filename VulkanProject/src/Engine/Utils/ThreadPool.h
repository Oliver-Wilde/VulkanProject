// ============================================================================
// ThreadPool.h – work-stealing pool with runtime meshing-limit control
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
#include <cstdint>      // for uint8_t

// ─── task metadata ─────────────────────────────────────────────────────────
enum class TaskType : uint8_t { Meshing, Generation };

struct Task
{
    std::function<void()> func;
    TaskType              type;
    int                   priority;
};

struct alignas(64) WorkDeque          // one per worker – cache-line aligned
{
    std::deque<Task> q;               // LIFO for owner, FIFO for stealers
    std::mutex       mtx;
};

class ThreadPool
{
public:
    /*  If threadCount == 0  →  uses  hardware_concurrency()-1  (min 1)       */
    ThreadPool(size_t threadCount = 0,
        size_t maxMeshTasks = 4,
        size_t maxGenTasks = 4);
    ~ThreadPool();

    /*  Queue a job – higher  priority  values are preferred when stealing    */
    void enqueueTask(const std::function<void()>& fn,
        TaskType type,
        int priority = 0);

    /*  Graceful shutdown – wakes workers, waits for them to finish           */
    void shutdown();

    // ── runtime introspection ───────────────────────────────────────────────
    size_t getThreadCount() const { return m_workers.size(); }
    size_t getQueueSize();                     // total outstanding tasks

    // ── dynamic meshing-concurrency control (slider in Debug UI) ───────────
    size_t getMaxMeshing() const;              // current hard-cap
    void   setMaxMeshing(size_t newLimit);     // atomic, RT-safe

private:
    void workerMain(size_t index);             // main loop of each worker
    bool tryPopLocal(size_t idx, Task& out);   // LIFO pop from own deque
    bool trySteal(size_t thiefIdx, Task& out); // FIFO steal from others

    /*  One queue per thread – prevents mutex contention on push/pop          */
    std::vector<std::unique_ptr<WorkDeque>> m_queues;

    std::vector<std::thread> m_workers;

    std::atomic<bool>   m_shutdown{ false };
    std::atomic<size_t> m_rrEnq{ 0 };          // round-robin enqueue index

    // ── global limits (meshing can change live) ────────────────────────────
    std::atomic<size_t> m_maxMeshing;          // adjustable at runtime
    const size_t        m_maxGeneration;       // fixed at construction

    std::atomic<size_t> m_activeMeshing{ 0 };
    std::atomic<size_t> m_activeGeneration{ 0 };

    /*  Sleep gate – workers block here when idle                             */
    std::condition_variable m_cv;
    std::mutex              m_sleepMtx;
};
