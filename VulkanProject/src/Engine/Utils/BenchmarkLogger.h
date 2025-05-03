#pragma once
#ifdef BENCHMARK_MODE
/* --------------------------------------------------------------------------
   BenchmarkLogger  – central CSV writer for per-frame and per-chunk rows
   -------------------------------------------------------------------------- */
#include <fstream>
#include <string>
#include <mutex>
#include <cstdint>

   /* ── row layouts (POD) ──────────────────────────────────────────────────── */
struct FrameLogRow
{
    uint32_t frameNumber;
    double   timestampMs;
    float    dtMs;
    float    cpuRebuildMs;
    float    gpuBusyMs;
    uint64_t bytesUploaded;
    uint64_t uploadBudget;
    uint32_t chunksRebuilt;
    uint32_t drawCalls;
    uint32_t triangles;
    uint64_t vramLiveBytes;
};
struct ChunkLogRow
{
    uint32_t frameNumber;
    uint64_t chunkId;      // 64-bit Morton or XYZ
    uint32_t meshingUs;    // μs
    uint32_t vertexCount;
};

/* ── singleton ──────────────────────────────────────────────────────────── */
class BenchmarkLogger
{
public:
    static BenchmarkLogger& get();     // global accessor

    /** Must be called once at program start (after CLI parsed). */
    void initialise(const std::string& scenario,
        uint32_t          seed,
        uint32_t          durationSec,
        const std::string& hardwareId);

    /* fast, thread-safe row emitters */
    void logFrame(const FrameLogRow& r);
    void logChunk(const ChunkLogRow& r);
    void flush();                      // optional; files auto-flush in dtor

private:
    BenchmarkLogger() = default;
    ~BenchmarkLogger();

    std::ofstream _frames;
    std::ofstream _chunks;
    std::mutex    _mtx;
};
#endif /* BENCHMARK_MODE */
