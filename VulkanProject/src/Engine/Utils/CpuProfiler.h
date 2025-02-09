#pragma once
#include <pdh.h>

class CpuProfiler
{
public:
    CpuProfiler();
    ~CpuProfiler();

    // Returns the total CPU usage in percentage (0.0 - 100.0)
    float GetCpuUsage();

private:
    PDH_HQUERY m_cpuQuery;
    PDH_HCOUNTER m_cpuTotal;
};
