#include "CpuProfiler.h"
#include <stdexcept>
#include <chrono>
#include <iostream>

#pragma comment(lib, "pdh.lib")

static long long nowMicros()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto dur = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
}

// -----------------------------------------------------------------------------
// Define the static map
// -----------------------------------------------------------------------------
std::unordered_map<std::string, ProfileRecord> CpuProfiler::s_profileData;

// -----------------------------------------------------------------------------
long long CpuProfiler::getCurrentTimeMicroseconds()
{
    return nowMicros();
}

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

// -----------------------------------------------------------------------------
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
CpuProfiler::ScopedTimer::ScopedTimer(const std::string& label)
    : m_label(label)
{
    m_startTimeMicroseconds = getCurrentTimeMicroseconds();
}

CpuProfiler::ScopedTimer::~ScopedTimer()
{
    long long endTime = getCurrentTimeMicroseconds();
    long long delta = endTime - m_startTimeMicroseconds;
    double ms = static_cast<double>(delta) / 1000.0;

    // Instead of printing, store the results in s_profileData
    auto& record = s_profileData[m_label];
    record.lastTimeMs = ms;
    record.accumTimeMs += ms;
    record.callCount++;
}

// -----------------------------------------------------------------------------
const std::unordered_map<std::string, ProfileRecord>& CpuProfiler::GetProfileRecords()
{
    return s_profileData;
}
