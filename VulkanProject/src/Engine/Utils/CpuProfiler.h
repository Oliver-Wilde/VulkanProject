#pragma once

#include <windows.h>
#include <pdh.h>
#include <vector>
#include <string>
#include <unordered_map>

// A small struct to keep track of timing data for each label.
struct ProfileRecord
{
    double lastTimeMs = 0.0;  // time of the most recent scope
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

    //----------------------------------------------------------------------
    // Nested struct for scope-based timing
    //----------------------------------------------------------------------
    struct ScopedTimer
    {
        ScopedTimer(const std::string& label);
        ~ScopedTimer();

    private:
        std::string m_label;
        long long   m_startTimeMicroseconds;
    };

    // ---------------------------------------------------------------------
    // Access the recorded times. We'll store these in a static map,
    // so your ImGui code can read them.
    // ---------------------------------------------------------------------
    static const std::unordered_map<std::string, ProfileRecord>& GetProfileRecords();

private:
    PDH_HQUERY   m_cpuQuery = nullptr;
    PDH_HCOUNTER m_cpuTotal = nullptr;

    static const size_t kMaxFPSamples = 100;
    std::vector<float>  m_fpsSamples;
    size_t              m_nextSampleIndex = 0;

    // Provide a helper for current time in microseconds
    static long long getCurrentTimeMicroseconds();

    // We'll keep the profile data in a static container so we can reference it anywhere
    static std::unordered_map<std::string, ProfileRecord> s_profileData;
};
