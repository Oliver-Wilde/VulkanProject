#pragma once

#include <string>

#ifdef BENCHMARK_MODE
#include <chrono>
#include <cstdint>
#include "Engine/Utils/ScenarioRunner.h"   // need enum Type for the member below
#endif

// ── forward declarations ──────────────────────────────────────────────
class Window;
class Time;
class VulkanContext;
class Renderer;
class Camera;
class VoxelWorld;
class ResourceManager;

class Application
{
public:
    Application();
    ~Application();

#ifdef BENCHMARK_MODE
    /* parse CLI flags: --scenario, --seed, --seconds */
    void parseCommandLine(int argc, char** argv);
#endif

    void init();
    void runLoop();
    void cleanup();

private:
    void handleInput(Camera& cam, float dt);

    /* ── core engine pointers ─────────────────────────────────────────── */
    Window* m_window = nullptr;
    Time* m_time = nullptr;
    VulkanContext* m_vulkanCtx = nullptr;
    Renderer* m_renderer = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;
    ResourceManager* m_resourceManager = nullptr;

    bool            m_isRunning = false;

#ifdef BENCHMARK_MODE
    /* ── benchmark-mode additional state ─────────────────────────────── */
    std::string                             m_scenario = "static";
    uint32_t                                m_seed = 1;
    uint32_t                                m_runSeconds = 60;
    std::chrono::steady_clock::time_point   m_startTime;

    /* chosen scenario type stored until camera exists */
    ScenarioRunner::Type                    m_pendingScenarioType =
        ScenarioRunner::Type::Static;

    ScenarioRunner* m_scenarioRunner = nullptr;
#endif
};
