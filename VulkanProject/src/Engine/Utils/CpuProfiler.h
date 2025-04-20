#pragma once

#include <windows.h>
#include <pdh.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint> // for uint32_t, etc.

// A small struct to keep track of timing data for each label.
struct ProfileRecord
{
    double lastTimeMs = 0.0;   // time of the most recent scope
    double accumTimeMs = 0.0;  // accumulated time across calls
    int    callCount = 0;    // how many times we've profiled
};

class CpuProfiler
{
public:
    CpuProfiler();
    ~CpuProfiler();

    float GetCpuUsage();

    void UpdateFPS(float fps);
    float GetRollingAverageFPS() const;

    // ---------------------------------------------------------------------
    // ScopedTimer for measuring code blocks. Results go into s_profileData.
    // ---------------------------------------------------------------------
    struct ScopedTimer
    {
        ScopedTimer(const std::string& label);
        ~ScopedTimer();

    private:
        std::string m_label;
        long long   m_startTimeMicroseconds;
    };

    // ---------------------------------------------------------------------
    // Access all recorded scope-timing data (e.g. for ImGui display).
    // ---------------------------------------------------------------------
    static const std::unordered_map<std::string, ProfileRecord>& GetProfileRecords();

    // ---------------------------------------------------------------------
    // New: CSV Logging for Performance
    //
    // 1) Start/Stop control
    // 2) Logging calls (e.g., once per frame or chunk event)
    // ---------------------------------------------------------------------
    static void StartLogging(const std::string& csvFilePath);
    static void StopLogging();
    static bool IsLogging();

    // Log frame-level stats (e.g., fps, CPU usage, GPU usage, draw calls, etc.)
    static void LogFrameStats(
        uint64_t frameNumber,
        float dt,
        float fps,
        float avgFps,
        float cpuUsage,
        float avgCpuUsage,
        uint32_t drawCalls,
        uint32_t totalVertices,
        size_t cpuMemBytes,
        size_t gpuMemBytes
    );

    // Log chunk generation timing
    static void LogChunkGeneration(
        int chunkX, int chunkY, int chunkZ,
        double genTimeSeconds
    );

    // Log chunk meshing timing
    static void LogChunkMeshing(
        int chunkX, int chunkY, int chunkZ,
        double meshTimeSeconds
    );

private:
    PDH_HQUERY   m_cpuQuery = nullptr;
    PDH_HCOUNTER m_cpuTotal = nullptr;

    static const size_t kMaxFPSamples = 100;
    std::vector<float>  m_fpsSamples;
    size_t              m_nextSampleIndex = 0;

    // Helper for current time in microseconds
    static long long getCurrentTimeMicroseconds();

    // Recorded scope-based timing data
    static std::unordered_map<std::string, ProfileRecord> s_profileData;

    // ---------------------------------------------------------------------
    // New: CSV Logging internals
    // ---------------------------------------------------------------------
    static bool         s_isLogging;    // Are we currently writing out CSV logs?
    static std::string  s_csvFilePath;  // Path to the log file
    static FILE* s_csvFile;      // File handle for output
};
