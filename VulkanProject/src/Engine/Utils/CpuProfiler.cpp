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
