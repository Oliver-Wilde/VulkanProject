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
#include "Engine/Resources/ResourceManager.h"   // ResourceManager location
#include "Engine/Utils/ThreadPool.h"            // global pool

#include <stdexcept>
#include <iostream>

ThreadPool g_threadPool(0);    // ------------------------------------------------

/* ============================================================================ */
/* ctor / dtor                                                                  */
/* ============================================================================ */
Application::Application() = default;

Application::~Application()
{
    cleanup();
}

/* ============================================================================ */
/* init – full engine bring‑up                                                  */
/* ============================================================================ */
void Application::init()
{
    /* 1) Voxel registry ---------------------------------------------------- */
    registerAllVoxels();
    std::cout << "DEBUG: Registered all voxel types.\n";

    /* 2) OS window --------------------------------------------------------- */
    m_window = new Window(800, 600, "My Voxel Engine");
    std::cout << "DEBUG: Created Window\n";

    /* 3) Time singleton ---------------------------------------------------- */
    m_time = new Time();

    /* 4) Vulkan context ---------------------------------------------------- */
    m_vulkanCtx = new VulkanContext();
    m_vulkanCtx->init(m_window);

    /* 5) Resources --------------------------------------------------------- */
    m_resourceManager = new ResourceManager(m_vulkanCtx);

    /* 6) World ------------------------------------------------------------- */
    m_voxelWorld = new VoxelWorld(m_vulkanCtx, m_resourceManager);

    /* 7) Renderer ---------------------------------------------------------- */
    m_renderer = new Renderer(m_vulkanCtx, m_window, m_voxelWorld);

    /* 8) Hook the two together (renderer must exist first) ----------------- */
    m_voxelWorld->setRenderer(m_renderer);

    /* 9) Misc -------------------------------------------------------------- */
    m_renderer->setTime(m_time);
    m_voxelWorld->initWorld();

    m_isRunning = true;
}

/* ============================================================================ */
/* handleInput – binds WASD & mouse to Camera                                   */
/* ============================================================================ */
void Application::handleInput(Camera& cam, float dt)
{
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
    Camera camera(glm::vec3(8.0f, 8.0f, 30.0f));
    bool wireframeWasPressed = false;

    while (m_isRunning && !m_window->shouldClose())
    {
        m_window->pollEvents();
        m_time->update();
        float dt = m_time->getDeltaTime();

        /* 1) input --------------------------------------------------------- */
        handleInput(camera, dt);

        /* 2) world update --------------------------------------------------- */
        if (m_voxelWorld)
            m_voxelWorld->updateChunksAroundPlayer(camera.position.x,
                camera.position.z);

        /* 3) toggle wireframe --------------------------------------------- */
        bool wireframeIsPressed =
            (glfwGetKey(m_window->getGLFWwindow(), GLFW_KEY_F) == GLFW_PRESS);
        if (wireframeIsPressed && !wireframeWasPressed)
        {
            Logger::Info("Toggling wireframe...");
            m_renderer->toggleWireframe();
        }
        wireframeWasPressed = wireframeIsPressed;

        /* 4) render -------------------------------------------------------- */
        m_renderer->setCamera(camera);
        m_renderer->renderFrame();
    }
}

/* ============================================================================ */
/* cleanup – reverse‑order teardown                                             */
/* ============================================================================ */
void Application::cleanup()
{
    /* 1) voxel world (needs renderer alive) ------------------------------- */
    delete m_voxelWorld;     m_voxelWorld = nullptr;

    /* 2) renderer --------------------------------------------------------- */
    delete m_renderer;       m_renderer = nullptr;

    /* 3) resources -------------------------------------------------------- */
    delete m_resourceManager; m_resourceManager = nullptr;

    /* 4) Vulkan context --------------------------------------------------- */
    if (m_vulkanCtx) { m_vulkanCtx->cleanup(); delete m_vulkanCtx; }
    m_vulkanCtx = nullptr;

    /* 5) window / time ---------------------------------------------------- */
    delete m_window; m_window = nullptr;
    delete m_time;   m_time = nullptr;

    m_isRunning = false;
}
