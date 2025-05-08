#include "Application.h"
#include "Window.h"
#include "Time.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Graphics/Renderer.h"
#include <GLFW/glfw3.h>
#include "Engine/Scene/Camera.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/VoxelSetup.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Utils/ThreadPool.h"
#include "Engine/Utils/CpuProfiler.h"

#ifdef BENCHMARK_MODE
#include "Engine/Utils/BenchmarkLogger.h"
#include "Engine/Utils/ScenarioRunner.h"
#include <chrono>
#endif

#include <stdexcept>
#include <iostream>


/* ── CLI params made global so any subsystem can read them ───────────── */
std::string g_cliMesher = "greedy";                        // greedy | naive
bool        g_cliCulling = true;                            // on | off
uint32_t    g_cliWorkers = std::thread::hardware_concurrency();
uint32_t    g_cliUploadMB = 8;                               // MiB/frame
int         g_cliViewDist = 8;


ThreadPool g_threadPool(/*threads=*/g_cliWorkers,
    /*maxMeshTasks=*/4,
    /*maxGenTasks=*/2);

/* ============================================================================ */
/* ctor / dtor                                                                  */
/* ============================================================================ */
Application::Application() = default;

Application::~Application()
{
    cleanup();
}

/* ============================================================================ */
/* parseCommandLine – BENCHMARK-only                                            */
/* ============================================================================ */
#ifdef BENCHMARK_MODE
void Application::parseCommandLine(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char*
            {
                if (i + 1 < argc) return argv[++i];
                Logger::Info("Missing value after '" + a + "'");
                return "";
            };

        if (a == "--scenario")   m_scenario = next();
        else if (a == "--seed")       m_seed = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--seconds")    m_runSeconds = static_cast<uint32_t>(std::stoul(next()));

        /* new benchmark flags -------------------------------------------- */
        else if (a == "--mesher")     g_cliMesher = next();              // greedy|naive
        else if (a == "--culling")    g_cliCulling = std::string(next()) == "on";
        else if (a == "--workers")    g_cliWorkers = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--upload-mib") g_cliUploadMB = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--viewdist")   g_cliViewDist = std::stoi(next());
        /* silently ignore unknown args                                    */
    }

    /* apply worker count parsed above */
    g_threadPool.resize(g_cliWorkers);   // harmless no-op if unchanged
}
#endif

/* ============================================================================ */
/* init – full engine bring-up                                                  */
/* ============================================================================ */
void Application::init()
{
    CpuProfiler::ScopedTimer initTimer("Application::init");

    /* 1) voxel registry -------------------------------------------------- */
    registerAllVoxels();

    /* 2) OS window ------------------------------------------------------- */
    m_window = new Window(800, 600, "My Voxel Engine");

    /* 3) Time singleton -------------------------------------------------- */
    m_time = new Time();

    /* 4) Vulkan context -------------------------------------------------- */
    m_vulkanCtx = new VulkanContext();
    m_vulkanCtx->init(m_window);

    /* 5) resources ------------------------------------------------------- */
    m_resourceManager = new ResourceManager(m_vulkanCtx);

    /* 6) world ----------------------------------------------------------- */
    m_voxelWorld = new VoxelWorld(m_vulkanCtx, m_resourceManager);

    /* 7) renderer -------------------------------------------------------- */
    m_renderer = new Renderer(m_vulkanCtx, m_window, m_voxelWorld);
    m_renderer->enableFrustumCulling(g_cliCulling);           // <<< new flag hook

    /* 8) wire them together --------------------------------------------- */
    m_voxelWorld->setRenderer(m_renderer);

    /* 9) misc ------------------------------------------------------------ */
    m_renderer->setTime(m_time);
    m_voxelWorld->initWorld();

#ifdef BENCHMARK_MODE
    /* ------------------------------ benchmark start -------------------- */
    if (m_runSeconds == 0) m_runSeconds = 60;
    const std::string hwId = Renderer::queryHardwareString();
    BenchmarkLogger::get().initialise(m_scenario, m_seed, m_runSeconds, hwId);

    ScenarioRunner::Type t = ScenarioRunner::Type::Static;
    if (m_scenario == "fly")  t = ScenarioRunner::Type::Fly;
    else if (m_scenario == "edit") t = ScenarioRunner::Type::Edit;
    else if (m_scenario == "lod")  t = ScenarioRunner::Type::Lod;
    m_pendingScenarioType = t;
    m_startTime = std::chrono::steady_clock::now();
#endif

    m_isRunning = true;
}


/* ============================================================================ */
/* handleInput – binds WASD & mouse to Camera                                   */
/* ============================================================================ */
void Application::handleInput(Camera& cam, float dt)
{
#ifdef BENCHMARK_MODE
    // In benchmark mode we disable manual camera & mouse so the ScenarioRunner
    // can drive everything deterministically.
    (void)cam; (void)dt;
    return;
#endif

    CpuProfiler::ScopedTimer inputTimer("Application::handleInput");

    GLFWwindow* window = m_window->getGLFWwindow();

    /* movement ------------------------------------------------------------- */
    glm::vec3 dir(
        cos(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch)),
        sin(glm::radians(cam.pitch)),
        sin(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch)));
    glm::vec3 forward = glm::normalize(dir);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

    float speed = cam.moveSpeed * dt;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.position += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.position -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.position += right * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.position -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cam.position.y += speed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cam.position.y -= speed;

    /* mouse look ----------------------------------------------------------- */
    static double lastX = 400, lastY = 300;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    float dx = static_cast<float>(mx - lastX);
    float dy = static_cast<float>(my - lastY);
    lastX = mx; lastY = my;

    cam.yaw += dx * cam.turnSpeed;
    cam.pitch -= dy * cam.turnSpeed;
    cam.pitch = std::clamp(cam.pitch, -89.f, 89.f);
}

/* ============================================================================ */
/* runLoop – main game loop                                                     */
/* ============================================================================ */
void Application::runLoop()
{
    CpuProfiler::ScopedTimer loopTimer("Application::runLoop");

    Camera camera(glm::vec3(8.0f, 8.0f, 30.0f));

#ifdef BENCHMARK_MODE
    /* create once, right after camera exists */
    if (!m_scenarioRunner)
        m_scenarioRunner = new ScenarioRunner(
            m_pendingScenarioType, m_seed, &camera, m_voxelWorld);
#endif
    bool   wireframeWasPressed = false;
    bool   rebuildWasPressed = false;

    while (m_isRunning && !m_window->shouldClose())
    {
        m_window->pollEvents();
        m_time->update();
        float dt = m_time->getDeltaTime();

#ifdef BENCHMARK_MODE
        /* automate camera / edits via ScenarioRunner ---------------------- */
        m_scenarioRunner->update(dt);
#else
        /* 1) manual input ------------------------------------------------- */
        handleInput(camera, dt);

        /* 1.5) instant chunk-rebuild hot-key ------------------------------ */
        if (glfwGetKey(m_window->getGLFWwindow(), GLFW_KEY_R) == GLFW_PRESS)
        {
            if (!rebuildWasPressed && m_voxelWorld)
                m_voxelWorld->forceRebuildAllChunks();
            rebuildWasPressed = true;
        }
        else rebuildWasPressed = false;

        /* 3) toggle wireframe --------------------------------------------- */
        bool wireframeIsPressed =
            (glfwGetKey(m_window->getGLFWwindow(), GLFW_KEY_F) == GLFW_PRESS);
        if (wireframeIsPressed && !wireframeWasPressed)
        {
            Logger::Info("Toggling wireframe...");
            m_renderer->toggleWireframe();
        }
        wireframeWasPressed = wireframeIsPressed;
#endif

        /* 2) world update --------------------------------------------------- */
        {
            CpuProfiler::ScopedTimer timer("VoxelWorld::updateChunksAroundPlayer");
            if (m_voxelWorld)
                m_voxelWorld->updateChunksAroundPlayer(camera.position.x,
                    camera.position.z);
        }

        /* 4) render -------------------------------------------------------- */
        m_renderer->setCamera(camera);
        m_renderer->renderFrame();

#ifdef BENCHMARK_MODE
        /* auto-quit when runSeconds elapse -------------------------------- */
        if (m_runSeconds > 0)
        {
            auto now = std::chrono::steady_clock::now();
            float sec = std::chrono::duration<float>(now - m_startTime).count();
            if (sec >= static_cast<float>(m_runSeconds))
                m_isRunning = false;
        }
#endif
    }
}

/* ============================================================================ */
/* cleanup – reverse-order teardown                                             */
/* ============================================================================ */
void Application::cleanup()
{
    CpuProfiler::ScopedTimer cleanupTimer("Application::cleanup");

#ifdef BENCHMARK_MODE
    delete m_scenarioRunner;  m_scenarioRunner = nullptr;
#endif

    g_threadPool.shutdown();

    delete m_voxelWorld;     m_voxelWorld = nullptr;
    delete m_renderer;       m_renderer = nullptr;
    delete m_resourceManager; m_resourceManager = nullptr;

    if (m_vulkanCtx) { m_vulkanCtx->cleanup(); delete m_vulkanCtx; }
    m_vulkanCtx = nullptr;

    delete m_window; m_window = nullptr;
    delete m_time;   m_time = nullptr;

    m_isRunning = false;
}
