#include "CpuProfiler.h"
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <cstdio>   // for FILE*, fopen, fprintf

#pragma comment(lib, "pdh.lib")

// -----------------------------------------------------------------------------
// Static definitions
// -----------------------------------------------------------------------------
std::unordered_map<std::string, ProfileRecord> CpuProfiler::s_profileData;
bool         CpuProfiler::s_isLogging = false;
std::string  CpuProfiler::s_csvFilePath = "";
FILE* CpuProfiler::s_csvFile = nullptr;

// -----------------------------------------------------------------------------
// Helper: current time in microseconds
// -----------------------------------------------------------------------------
static long long nowMicros()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto dur = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
}

long long CpuProfiler::getCurrentTimeMicroseconds()
{
    return nowMicros();
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
CpuProfiler::CpuProfiler()
{
    // PDH Query Setup
    if (PdhOpenQuery(NULL, NULL, &m_cpuQuery) != ERROR_SUCCESS)
    {
        throw std::runtime_error("Failed to open PDH query for CPU usage.");
    }

    if (PdhAddCounter(m_cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &m_cpuTotal) != ERROR_SUCCESS)
    {
        throw std::runtime_error("Failed to add CPU usage counter.");
    }

    // Collect once to initialize
    PdhCollectQueryData(m_cpuQuery);
}

CpuProfiler::~CpuProfiler()
{
    if (m_cpuQuery)
    {
        PdhCloseQuery(m_cpuQuery);
        m_cpuQuery = nullptr;
    }
}

// -----------------------------------------------------------------------------
// CPU usage
// -----------------------------------------------------------------------------
float CpuProfiler::GetCpuUsage()
{
    if (PdhCollectQueryData(m_cpuQuery) != ERROR_SUCCESS)
    {
        return 0.0f;
    }

    PDH_FMT_COUNTERVALUE counterVal;
    if (PdhGetFormattedCounterValue(m_cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal) != ERROR_SUCCESS)
    {
        return 0.0f;
    }
    return static_cast<float>(counterVal.doubleValue);
}

// -----------------------------------------------------------------------------
// FPS tracking
// -----------------------------------------------------------------------------
void CpuProfiler::UpdateFPS(float fps)
{
    if (m_fpsSamples.size() < kMaxFPSamples)
    {
        m_fpsSamples.push_back(fps);
    }
    else
    {
        m_fpsSamples[m_nextSampleIndex] = fps;
        m_nextSampleIndex = (m_nextSampleIndex + 1) % kMaxFPSamples;
    }
}

float CpuProfiler::GetRollingAverageFPS() const
{
    if (m_fpsSamples.empty())
        return 0.0f;

    float sum = 0.0f;
    for (float sample : m_fpsSamples)
    {
        sum += sample;
    }
    return sum / static_cast<float>(m_fpsSamples.size());
}

// -----------------------------------------------------------------------------
// ScopedTimer for profiling code blocks
// -----------------------------------------------------------------------------
CpuProfiler::ScopedTimer::ScopedTimer(const std::string& label)
    : m_label(label)
{
    m_startTimeMicroseconds = nowMicros();
}

CpuProfiler::ScopedTimer::~ScopedTimer()
{
    long long endTime = nowMicros();
    long long delta = endTime - m_startTimeMicroseconds;
    double ms = static_cast<double>(delta) / 1000.0;

    auto& record = s_profileData[m_label];
    record.lastTimeMs = ms;
    record.accumTimeMs += ms;
    record.callCount++;
}

// -----------------------------------------------------------------------------
// Access recorded scope-timing data
// -----------------------------------------------------------------------------
const std::unordered_map<std::string, ProfileRecord>& CpuProfiler::GetProfileRecords()
{
    return s_profileData;
}

// -----------------------------------------------------------------------------
// CSV Logging: Start/Stop
// -----------------------------------------------------------------------------
void CpuProfiler::StartLogging(const std::string& csvFilePath)
{
    if (s_isLogging)
    {
        // Already logging; you could StopLogging() then StartLogging() again if needed
        return;
    }

    // Open file for writing (overwrite)
    s_csvFilePath = csvFilePath;
#ifdef _MSC_VER
    fopen_s(&s_csvFile, s_csvFilePath.c_str(), "w");
#else
    s_csvFile = std::fopen(s_csvFilePath.c_str(), "w");
#endif

    if (!s_csvFile)
    {
        std::cerr << "CpuProfiler::StartLogging => Failed to open file: "
            << s_csvFilePath << std::endl;
        s_isLogging = false;
        return;
    }

    s_isLogging = true;

    // Print header row. We combine possible columns for all events:
    // - Event type
    // - FrameNumber
    // - dt
    // - fps, avgFps
    // - cpuUsage, avgCpuUsage
    // - drawCalls, totalVertices
    // - cpuMemBytes, gpuMemBytes
    // - chunkX, chunkY, chunkZ
    // - genTimeSec, meshTimeSec
    //
    // Some columns may remain unused depending on the event row.
    // We can keep them blank or zero. 
    fprintf(s_csvFile,
        "Event,FrameNumber,dt,"
        "fps,avgFps,"
        "cpuUsage,avgCpuUsage,"
        "drawCalls,totalVertices,"
        "cpuMemBytes,gpuMemBytes,"
        "chunkX,chunkY,chunkZ,"
        "genTimeSec,meshTimeSec\n"
    );
    fflush(s_csvFile);
}

void CpuProfiler::StopLogging()
{
    if (!s_isLogging)
        return;

    if (s_csvFile)
    {
        std::fclose(s_csvFile);
        s_csvFile = nullptr;
    }
    s_isLogging = false;
}

bool CpuProfiler::IsLogging()
{
    return s_isLogging;
}

// -----------------------------------------------------------------------------
// CSV Logging: Frame Stats
// -----------------------------------------------------------------------------
void CpuProfiler::LogFrameStats(
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
)
{
    if (!s_isLogging || !s_csvFile)
        return;

    // CSV row for a "Frame" event. Many chunk columns remain blank.
    fprintf(s_csvFile,
        "Frame,%llu,%.4f,"     // Event, FrameNumber, dt
        "%.2f,%.2f,"          // fps, avgFps
        "%.1f,%.1f,"          // cpuUsage, avgCpuUsage
        "%u,%u,"              // drawCalls, totalVertices
        "%zu,%zu,"            // cpuMemBytes, gpuMemBytes
        ",,,,"                // chunkX, chunkY, chunkZ (empty)
        ",\n"                 // genTimeSec, meshTimeSec (empty)
        , static_cast<unsigned long long>(frameNumber)
        , dt
        , fps, avgFps
        , cpuUsage, avgCpuUsage
        , drawCalls, totalVertices
        , cpuMemBytes, gpuMemBytes
    );
    fflush(s_csvFile);
}

// -----------------------------------------------------------------------------
// CSV Logging: Chunk Generation
// -----------------------------------------------------------------------------
void CpuProfiler::LogChunkGeneration(
    int chunkX, int chunkY, int chunkZ,
    double genTimeSeconds
)
{
    if (!s_isLogging || !s_csvFile)
        return;

    // CSV row for a "ChunkGen" event
    // We can leave frame-based columns blank or 0. 
    // We fill chunk coords and generation time.
    fprintf(s_csvFile,
        "ChunkGen,,"           // Event=ChunkGen, FrameNumber=blank
        ",,,,,"                // dt, fps, avgFps, cpuUsage, avgCpuUsage=blank
        ",,"                   // drawCalls, totalVertices=blank
        ",,"                   // cpuMemBytes, gpuMemBytes=blank
        "%d,%d,%d,"            // chunkX, chunkY, chunkZ
        "%.6f,\n"             // genTimeSec, meshTimeSec=blank
        , chunkX, chunkY, chunkZ
        , genTimeSeconds
    );
    fflush(s_csvFile);
}

// -----------------------------------------------------------------------------
// CSV Logging: Chunk Meshing
// -----------------------------------------------------------------------------
void CpuProfiler::LogChunkMeshing(
    int chunkX, int chunkY, int chunkZ,
    double meshTimeSeconds
)
{
    if (!s_isLogging || !s_csvFile)
        return;

    // CSV row for a "ChunkMesh" event
    fprintf(s_csvFile,
        "ChunkMesh,,"          // Event=ChunkMesh, no FrameNumber
        ",,,,,"                // dt, fps, avgFps, cpuUsage, avgCpuUsage=blank
        ",,"                   // drawCalls, totalVertices=blank
        ",,"                   // cpuMemBytes, gpuMemBytes=blank
        "%d,%d,%d,"            // chunkX, chunkY, chunkZ
        ",%.6f\n"             // genTimeSec=blank, meshTimeSec=our input
        , chunkX, chunkY, chunkZ
        , meshTimeSeconds
    );
    fflush(s_csvFile);
}
