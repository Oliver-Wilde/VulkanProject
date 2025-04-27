// ============================================================================
// ThreadPool.cpp – work‑stealing pool with runtime meshing‑limit control
//                 Updated 2025‑04‑27  (implements get/setMaxMeshing)
// ============================================================================
#include "ThreadPool.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <memory>
#include <mutex>

// ───────────────────────────────── ctor / dtor ───────────────────────────
ThreadPool::ThreadPool(size_t threadCount,
    size_t maxMesh,
    size_t maxGen)
    : m_maxMeshing(maxMesh)
    , m_maxGeneration(maxGen)
{
    if (threadCount == 0)
    {
        size_t hc = std::thread::hardware_concurrency();
        threadCount = (hc > 2) ? hc - 1 : 1;
    }

    m_queues.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i)
        m_queues.emplace_back(std::make_unique<WorkDeque>());

    m_workers.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i)
        m_workers.emplace_back([this, i] { workerMain(i); });
}

ThreadPool::~ThreadPool() { shutdown(); }

// ───────────────────────────────── enqueueTask ───────────────────────────
void ThreadPool::enqueueTask(const std::function<void()>& fn,
    TaskType type,
    int priority)
{
    Task t{ fn, type, priority };
    size_t idx = m_rrEnq.fetch_add(1, std::memory_order_relaxed) % m_queues.size();

    {
        std::lock_guard<std::mutex> lk(m_queues[idx]->mtx);
        m_queues[idx]->q.push_back(std::move(t));
    }
    m_cv.notify_one();
}

// ───────────────────────────────── shutdown ──────────────────────────────
void ThreadPool::shutdown()
{
    bool expected = false;
    if (!m_shutdown.compare_exchange_strong(expected, true)) return;

    m_cv.notify_all();
    for (auto& w : m_workers)
        if (w.joinable()) w.join();
}

// ───────────────────────────────── getQueueSize ──────────────────────────
size_t ThreadPool::getQueueSize()
{
    size_t total = 0;
    for (auto& dqPtr : m_queues)
    {
        std::lock_guard<std::mutex> lk(dqPtr->mtx);
        total += dqPtr->q.size();
    }
    return total;
}

// NEW: runtime meshing limit accessors ------------------------------------
size_t ThreadPool::getMaxMeshing() const { return m_maxMeshing.load(std::memory_order_relaxed); }
void   ThreadPool::setMaxMeshing(size_t v) { m_maxMeshing.store(v, std::memory_order_relaxed); }

// ───────────────────────────────── workerMain ────────────────────────────
void ThreadPool::workerMain(size_t idx)
{
    while (!m_shutdown.load(std::memory_order_relaxed))
    {
        Task job;
        bool haveJob = tryPopLocal(idx, job) || trySteal(idx, job);

        if (!haveJob)
        {
            std::unique_lock<std::mutex> lk(m_sleepMtx);
            m_cv.wait_for(lk, std::chrono::milliseconds(2));
            continue;
        }

        auto* counter = (job.type == TaskType::Meshing) ? &m_activeMeshing
            : &m_activeGeneration;
        const size_t limit = (job.type == TaskType::Meshing)
            ? m_maxMeshing.load(std::memory_order_relaxed)
            : m_maxGeneration;

        size_t prev = counter->fetch_add(1, std::memory_order_acquire);
        if (prev >= limit)
        {
            counter->fetch_sub(1, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(m_queues[idx]->mtx);
                m_queues[idx]->q.push_front(std::move(job));
            }
            std::this_thread::yield();
            continue;
        }

        job.func();
        counter->fetch_sub(1, std::memory_order_release);
    }
}

// ───────────────────────────────── local pop (LIFO) ──────────────────────
bool ThreadPool::tryPopLocal(size_t idx, Task& out)
{
    WorkDeque& dq = *m_queues[idx];
    std::lock_guard<std::mutex> lk(dq.mtx);
    if (dq.q.empty()) return false;
    out = std::move(dq.q.back());
    dq.q.pop_back();
    return true;
}

// ───────────────────────────────── steal (FIFO) ──────────────────────────
bool ThreadPool::trySteal(size_t thiefIdx, Task& out)
{
    size_t n = m_queues.size();
    for (size_t off = 1; off < n; ++off)
    {
        size_t victim = (thiefIdx + off) % n;
        WorkDeque& dq = *m_queues[victim];
        std::lock_guard<std::mutex> lk(dq.mtx);
        if (dq.q.empty()) continue;
        out = std::move(dq.q.front());
        dq.q.pop_front();
        return true;
    }
    return false;
}
