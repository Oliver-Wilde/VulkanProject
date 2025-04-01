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

#include <stdexcept>
#include <iostream>

// Optional global thread pool
#include "Engine/Utils/ThreadPool.h"
ThreadPool g_threadPool(0);

Application::Application()
{
}

Application::~Application()
{
    cleanup();
}

void Application::init()
{
    // 1) Register voxel types
    registerAllVoxels();
    std::cout << "DEBUG: Registered all voxel types.\n";

    // 2) Create Window
    m_window = new Window(800, 600, "My Voxel Engine");
    std::cout << "DEBUG: Created Window\n";

    // 3) Create Time
    m_time = new Time();

    // 4) Vulkan context
    m_vulkanCtx = new VulkanContext();
    m_vulkanCtx->init(m_window);

    // 5) Create ResourceManager
    m_resourceManager = new ResourceManager(m_vulkanCtx);

    // 6) Create VoxelWorld
    m_voxelWorld = new VoxelWorld(m_vulkanCtx, m_resourceManager);
    m_voxelWorld->initWorld();

    // 7) Create Renderer
    m_renderer = new Renderer(m_vulkanCtx, m_window, m_voxelWorld);
    // Let voxelWorld know about renderer if needed
    m_voxelWorld->setRenderer(m_renderer);

    m_renderer->setTime(m_time);

    m_isRunning = true;
}

void Application::handleInput(Camera& cam, float dt)
{
    GLFWwindow* window = m_window->getGLFWwindow();

    glm::vec3 direction(
        cos(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch)),
        sin(glm::radians(cam.pitch)),
        sin(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch))
    );
    glm::vec3 forward = glm::normalize(direction);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

    float speed = cam.moveSpeed * dt;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.position += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.position -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.position += right * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.position -= right * speed;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        cam.position.y += speed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)   cam.position.y -= speed;

    // Mouse look
    static double lastX = 400, lastY = 300;
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    float dx = static_cast<float>(mouseX - lastX);
    float dy = static_cast<float>(mouseY - lastY);
    lastX = mouseX;
    lastY = mouseY;

    cam.yaw += dx * cam.turnSpeed;
    cam.pitch -= dy * cam.turnSpeed;
    if (cam.pitch > 89.f)   cam.pitch = 89.f;
    if (cam.pitch < -89.f)  cam.pitch = -89.f;
}

void Application::runLoop()
{
    Camera camera(glm::vec3(8.0f, 8.0f, 30.0f));
    bool wireframeWasPressed = false;

    size_t frameCount = 0;

    while (m_isRunning && !m_window->shouldClose())
    {
        m_window->pollEvents();
        m_time->update();
        float dt = m_time->getDeltaTime();

        // 1) camera input
        handleInput(camera, dt);

        // 2) update world around camera
        if (m_voxelWorld) {
            m_voxelWorld->updateChunksAroundPlayer(camera.position.x, camera.position.z);
        }

        // 3) toggle wireframe with 'F'
        bool wireframeIsPressed =
            (glfwGetKey(m_window->getGLFWwindow(), GLFW_KEY_F) == GLFW_PRESS);
        if (wireframeIsPressed && !wireframeWasPressed) {
            Logger::Info("Toggling wireframe...");
            m_renderer->toggleWireframe();
        }
        wireframeWasPressed = wireframeIsPressed;

        // One-frame-late approach for occlusion queries:
        if (frameCount > 0)
        {
            // gather results from previous frame's pass
            m_renderer->gatherOcclusionResults();
        }

        // 4) Render => includes resetting queries & new occlusion pass
        m_renderer->setCamera(camera);
        m_renderer->renderFrame();

        frameCount++;
    }
}

void Application::cleanup()
{
    // 1) Destroy voxel world
    if (m_voxelWorld) {
        delete m_voxelWorld;
        m_voxelWorld = nullptr;
    }

    // 2) Flush any leftover chunk buffers so they're destroyed
    if (m_renderer) {
        m_renderer->freeDeferredResources();
    }

    // 3) Destroy renderer
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }

    // 4) Destroy resource manager
    if (m_resourceManager) {
        delete m_resourceManager;
        m_resourceManager = nullptr;
    }

    // 5) Vulkan context
    if (m_vulkanCtx) {
        m_vulkanCtx->cleanup();
        delete m_vulkanCtx;
        m_vulkanCtx = nullptr;
    }

    // 6) Window
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }

    // 7) Time
    if (m_time) {
        delete m_time;
        m_time = nullptr;
    }

    // optional: g_threadPool.shutdown();

    m_isRunning = false;
}
