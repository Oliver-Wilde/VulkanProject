#include "CpuProfiler.h"
#include <stdexcept>

// Link with pdh.lib (for Visual Studio)
#pragma comment(lib, "pdh.lib")

CpuProfiler::CpuProfiler()
{
    // Open a PDH query
    if (PdhOpenQuery(NULL, NULL, &m_cpuQuery) != ERROR_SUCCESS) {
        throw std::runtime_error("Failed to open PDH query for CPU usage.");
    }

    // Add a counter for total CPU usage
    if (PdhAddCounter(m_cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &m_cpuTotal) != ERROR_SUCCESS)
    {
        throw std::runtime_error("Failed to add CPU usage counter.");
    }

    // Do an initial collection
    PdhCollectQueryData(m_cpuQuery);
}

CpuProfiler::~CpuProfiler()
{
    PdhCloseQuery(m_cpuQuery);
}

float CpuProfiler::GetCpuUsage()
{
    // Collect data for the query
    if (PdhCollectQueryData(m_cpuQuery) != ERROR_SUCCESS) {
        return 0.0f;
    }

    PDH_FMT_COUNTERVALUE counterVal;
    if (PdhGetFormattedCounterValue(m_cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal) != ERROR_SUCCESS) {
        return 0.0f;
    }
    return static_cast<float>(counterVal.doubleValue);
}

// --- New rolling average FPS implementation ---

void CpuProfiler::UpdateFPS(float fps)
{
    // If we haven't filled our sample buffer yet, add the new FPS sample.
    if (m_fpsSamples.size() < kMaxFPSamples) {
        m_fpsSamples.push_back(fps);
    }
    else {
        // Replace the oldest sample with the new one.
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
