// ============================================================================
// BenchFlags.h  – Global CLI-driven tuning knobs for benchmark mode
// ----------------------------------------------------------------------------
//  Declared once, defined in Application.cpp, included everywhere else.
// ============================================================================

#pragma once
#include <string>

// ----------------------------- CLI flags -----------------------------------
// All are populated by Application::parseCommandLine() **before init() runs**
// and remain constant for the entire process lifetime.

extern std::string g_cliMesher;      // "greedy" | "naive"
extern bool        g_cliCulling;     // true = culling ON, false = OFF
extern uint32_t    g_cliWorkers;     // logical worker threads in ThreadPool
extern uint32_t    g_cliUploadMB;    // GPU upload budget per frame (MiB)
extern int         g_cliViewDist;    // chunk radius (4-12 typical)
