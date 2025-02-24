#pragma once
#include <pdh.h>
#include <vector>

class CpuProfiler
{
public:
    CpuProfiler();
    ~CpuProfiler();

    // Returns the total CPU usage in percentage (0.0 - 100.0)
    float GetCpuUsage();

    // --- New methods for rolling average FPS ---
    // Call this once per frame with the current FPS value.
    void UpdateFPS(float fps);

    // Returns the rolling average FPS based on the stored samples.
    float GetRollingAverageFPS() const;

private:
    PDH_HQUERY m_cpuQuery;
    PDH_HCOUNTER m_cpuTotal;

    // Rolling average FPS variables:
    static const size_t kMaxFPSamples = 60; // Number of frames to average over
    std::vector<float> m_fpsSamples;
    size_t m_nextSampleIndex = 0; // For circular buffer replacement
};
