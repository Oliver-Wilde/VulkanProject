#ifdef BENCHMARK_MODE
#pragma once
#include <cstdint>
#include <mutex>
#include <fstream>
#include <string>

class BenchmarkLogger
{
public:
    /* ── public helpers ─────────────────────────────────────────────── */
    struct FrameLogRow
    {
        uint32_t frameNumber;
        double   timestampMs;
        float    dtMs;

        /* core title metrics */
        float    cpuRebuildMs;
        float    gpuBusyMs;
        uint32_t bytesUploaded;
        uint32_t uploadBudget;
        uint32_t chunksRebuilt;

        /* rendering load */
        uint32_t drawCalls;
        uint32_t triangles;

        /* memory footprint */
        uint64_t vramLiveBytes;
        uint64_t cpuMemBytes;         // NEW
    };
    struct ChunkLogRow
    {
        uint32_t frameNumber;
        uint32_t chunkId;
        uint32_t meshingUs;
        uint32_t vertexCount;
    };

    static BenchmarkLogger& get();

    void initialise(const std::string& scenario,
        uint32_t seed,
        uint32_t durationSec,
        const std::string& hwId);

    void logFrame(const FrameLogRow& r);
    void logChunk(const ChunkLogRow& r);
    void flush();
    ~BenchmarkLogger();

private:
    BenchmarkLogger() = default;
    std::mutex  _mtx;
    std::ofstream _frames, _chunks;
};
#endif /* BENCHMARK_MODE */
