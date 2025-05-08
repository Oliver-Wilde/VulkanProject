#ifdef BENCHMARK_MODE
#include "BenchmarkLogger.h"

#include "Engine/Globals/BenchFlags.h"      // ← access g_cliMesher / … flags

#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

/* ── helpers ───────────────────────────────────────────────────────────── */
static std::string timestampForFilename()
{
    auto  now = std::chrono::system_clock::now();
    auto  t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

/* ── singleton accessor ───────────────────────────────────────────────── */
BenchmarkLogger& BenchmarkLogger::get()
{
    static BenchmarkLogger inst;
    return inst;
}

/* ── initialise (called once at programme start) ─────────────────────── */
void BenchmarkLogger::initialise(const std::string& scenario,
    uint32_t           seed,
    uint32_t           durationSec,
    const std::string& hwId)
{
    const std::string stamp = timestampForFilename();

    /* filename stem now carries EVERY tunable so Python can auto-group   */
    const std::string base = "bench_" + scenario +
        "_mesher" + g_cliMesher +
        "_cull" + (g_cliCulling ? "on" : "off") +
        "_workers" + std::to_string(g_cliWorkers) +
        "_up" + std::to_string(g_cliUploadMB) +
        "_view" + std::to_string(g_cliViewDist) +
        "_seed" + std::to_string(seed) +
        "_dur" + std::to_string(durationSec) +
        "_" + stamp + "_";

    _frames.open(base + "frames.csv", std::ios::out | std::ios::trunc);
    _chunks.open(base + "chunks.csv", std::ios::out | std::ios::trunc);

    /* meta header (first line, begins with ‘#’) -- same keys as filename  */
    const std::string meta =
        "#scenario=" + scenario +
        ",seed=" + std::to_string(seed) +
        ",durationSec=" + std::to_string(durationSec) +
        ",mesher=" + g_cliMesher +
        ",culling=" + (g_cliCulling ? "on" : "off") +
        ",workers=" + std::to_string(g_cliWorkers) +
        ",uploadMiB=" + std::to_string(g_cliUploadMB) +
        ",viewDist=" + std::to_string(g_cliViewDist) +
        ",commit=" + GIT_COMMIT_HASH +
        ",hardware=" + hwId + "\n";

    _frames << meta;
    _chunks << meta;

    /* CSV headers (frames gains cpuMemBytes)                             */
    _frames << "frameNumber,timestampMs,dtMs,cpuRebuildMs,gpuBusyMs,"
        "bytesUploaded,uploadBudget,chunksRebuilt,drawCalls,"
        "triangles,vramLiveBytes,cpuMemBytes\n";
    _chunks << "frameNumber,chunkId,meshingUs,vertexCount\n";
}

/* ── row writers (unchanged apart from new column already handled) ───── */
void BenchmarkLogger::logFrame(const FrameLogRow& r)
{
    std::lock_guard<std::mutex> g(_mtx);
    _frames << r.frameNumber << ',' << r.timestampMs << ','
        << r.dtMs << ',' << r.cpuRebuildMs << ','
        << r.gpuBusyMs << ',' << r.bytesUploaded << ','
        << r.uploadBudget << ',' << r.chunksRebuilt << ','
        << r.drawCalls << ',' << r.triangles << ','
        << r.vramLiveBytes << ',' << r.cpuMemBytes << '\n';
}

void BenchmarkLogger::logChunk(const ChunkLogRow& r)
{
    std::lock_guard<std::mutex> g(_mtx);
    _chunks << r.frameNumber << ',' << r.chunkId << ','
        << r.meshingUs << ',' << r.vertexCount << '\n';
}

void BenchmarkLogger::flush()
{
    std::lock_guard<std::mutex> g(_mtx);
    _frames.flush();
    _chunks.flush();
}

BenchmarkLogger::~BenchmarkLogger()
{
    flush();
    _frames.close();
    _chunks.close();
}
#endif /* BENCHMARK_MODE */
