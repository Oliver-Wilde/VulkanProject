#include "CpuProfiler.h"
#include <chrono>
#include <cstdio>      // FILE*, fopen
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
#endif

/* ═══════════════════════════════════════════════════════════════════════ */
/* static data                                                             */
/* ═══════════════════════════════════════════════════════════════════════ */
std::unordered_map<std::string, ProfileRecord> CpuProfiler::s_profileData;
bool         CpuProfiler::s_isLogging = false;
std::string  CpuProfiler::s_csvFilePath = "";
FILE* CpuProfiler::s_csvFile = nullptr;

/* helper – microsecond timer */
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

/* ═══════════════════════════════════════════════════════════════════════ */
/* ctor / dtor                                                             */
/* ═══════════════════════════════════════════════════════════════════════ */
CpuProfiler::CpuProfiler()
{
#ifdef _WIN32
    /* PDH query setup ---------------------------------------------------- */
    if (PdhOpenQuery(nullptr, 0, &m_cpuQuery) != ERROR_SUCCESS)
        throw std::runtime_error("CpuProfiler: PdhOpenQuery failed");

    if (PdhAddCounter(m_cpuQuery,
        L"\\Processor(_Total)\\% Processor Time",
        0, &m_cpuTotal) != ERROR_SUCCESS)
        throw std::runtime_error("CpuProfiler: PdhAddCounter failed");

    PdhCollectQueryData(m_cpuQuery);      // prime the query
#else
    /* non‑Windows: nothing to set up */
    m_cpuQuery = nullptr;
#endif
}

CpuProfiler::~CpuProfiler()
{
#ifdef _WIN32
    if (m_cpuQuery) PdhCloseQuery(m_cpuQuery);
#endif
    m_cpuQuery = nullptr;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* CPU usage                                                               */
/* ═══════════════════════════════════════════════════════════════════════ */
float CpuProfiler::GetCpuUsage()
{
#ifdef _WIN32
    if (!m_cpuQuery) return 0.f;

    if (PdhCollectQueryData(m_cpuQuery) != ERROR_SUCCESS)
        return 0.f;

    PDH_FMT_COUNTERVALUE val{};
    if (PdhGetFormattedCounterValue(m_cpuTotal,
        PDH_FMT_DOUBLE, nullptr, &val) != ERROR_SUCCESS)
        return 0.f;
    return static_cast<float>(val.doubleValue);
#else
    /* TODO: add POSIX / proc‑stat path if needed */
    return 0.f;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* FPS rolling average helpers                                             */
/* ═══════════════════════════════════════════════════════════════════════ */
void CpuProfiler::UpdateFPS(float fps)
{
    if (m_fpsSamples.size() < kMaxFPSamples)
        m_fpsSamples.push_back(fps);
    else
    {
        m_fpsSamples[m_nextSampleIndex] = fps;
        m_nextSampleIndex = (m_nextSampleIndex + 1) % kMaxFPSamples;
    }
}

float CpuProfiler::GetRollingAverageFPS() const
{
    if (m_fpsSamples.empty()) return 0.f;
    float sum = 0.f;
    for (float s : m_fpsSamples) sum += s;
    return sum / static_cast<float>(m_fpsSamples.size());
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* ScopedTimer implementation                                              */
/* ═══════════════════════════════════════════════════════════════════════ */
CpuProfiler::ScopedTimer::ScopedTimer(const std::string& label)
    : m_label(label)
{
    m_startTimeMicroseconds = nowMicros();
}

CpuProfiler::ScopedTimer::~ScopedTimer()
{
    long long dt = nowMicros() - m_startTimeMicroseconds;
    double ms = static_cast<double>(dt) / 1000.0;

    auto& rec = s_profileData[m_label];
    rec.lastTimeMs = ms;
    rec.accumTimeMs += ms;
    rec.callCount++;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* public static – profile record access                                   */
/* ═══════════════════════════════════════════════════════════════════════ */
const std::unordered_map<std::string, ProfileRecord>&
CpuProfiler::GetProfileRecords()
{
    return s_profileData;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* CSV logging helpers                                                     */
/* ═══════════════════════════════════════════════════════════════════════ */
void CpuProfiler::StartLogging(const std::string& csvFilePath)
{
    if (s_isLogging) return;

    s_csvFilePath = csvFilePath;
#ifdef _MSC_VER
    fopen_s(&s_csvFile, s_csvFilePath.c_str(), "w");
#else
    s_csvFile = std::fopen(s_csvFilePath.c_str(), "w");
#endif
    if (!s_csvFile)
    {
        std::cerr << "CpuProfiler: cannot open " << s_csvFilePath << "\n";
        return;
    }

    s_isLogging = true;
    fprintf(s_csvFile,
        "Event,FrameNumber,dt,"
        "fps,avgFps,"
        "cpuUsage,avgCpuUsage,"
        "drawCalls,totalVertices,"
        "cpuMemBytes,gpuMemBytes,"
        "chunkX,chunkY,chunkZ,"
        "genTimeSec,meshTimeSec\n");
    fflush(s_csvFile);
}

void CpuProfiler::StopLogging()
{
    if (!s_isLogging) return;
    if (s_csvFile) { std::fclose(s_csvFile); s_csvFile = nullptr; }
    s_isLogging = false;
}

bool CpuProfiler::IsLogging() { return s_isLogging; }

/* frame / chunk logging helpers — unchanged from original file */
void CpuProfiler::LogFrameStats(
    uint64_t frameNumber, float dt,
    float fps, float avgFps,
    float cpuUsage, float avgCpuUsage,
    uint32_t drawCalls, uint32_t totalVertices,
    size_t cpuMemBytes, size_t gpuMemBytes)
{
    if (!s_isLogging || !s_csvFile) return;
    fprintf(s_csvFile,
        "Frame,%llu,%.4f,%.2f,%.2f,%.1f,%.1f,%u,%u,%zu,%zu,,,,\n",
        static_cast<unsigned long long>(frameNumber), dt,
        fps, avgFps, cpuUsage, avgCpuUsage,
        drawCalls, totalVertices, cpuMemBytes, gpuMemBytes);
    fflush(s_csvFile);
}

void CpuProfiler::LogChunkGeneration(int cx, int cy, int cz, double sec)
{
    if (!s_isLogging || !s_csvFile) return;
    fprintf(s_csvFile,
        "ChunkGen,, ,,,, ,,, ,,,%d,%d,%d,%.6f,\n", cx, cy, cz, sec);
    fflush(s_csvFile);
}

void CpuProfiler::LogChunkMeshing(int cx, int cy, int cz, double sec)
{
    if (!s_isLogging || !s_csvFile) return;
    fprintf(s_csvFile,
        "ChunkMesh,, ,,,, ,,, ,,,%d,%d,%d, ,%.6f\n", cx, cy, cz, sec);
    fflush(s_csvFile);
}
