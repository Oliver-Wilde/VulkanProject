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
#include <stdexcept>

Application::Application()
{
    // optionally set pointers to nullptr here
}

Application::~Application()
{
    cleanup();
}

void Application::init()
{
    // 1) Register voxel types (IDs for Stone, Grass, etc.)
    registerAllVoxels();

    // 2) Create window
    m_window = new Window(800, 600, "My Voxel Engine");

    // 3) Time / DeltaTime
    m_time = new Time();

    // 4) Vulkan context + init
    m_vulkanCtx = new VulkanContext();
    m_vulkanCtx->init(m_window);

    // 5) Create VoxelWorld
    m_voxelWorld = new VoxelWorld(m_vulkanCtx);
    m_voxelWorld->initWorld();

    // 6) Create Renderer
    m_renderer = new Renderer(m_vulkanCtx, m_window);

    m_renderer->setTime(m_time);

    m_isRunning = true;
}

void Application::handleInput(Camera& cam, float dt)
{
    GLFWwindow* window = m_window->getGLFWwindow();

    // Build forward vector
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

    // up/down
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        cam.position.y += speed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)   cam.position.y -= speed;

    // Mouse look
    static double lastX = 400, lastY = 300;
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    float dx = (float)(mouseX - lastX);
    float dy = (float)(mouseY - lastY);
    lastX = mouseX;
    lastY = mouseY;

    cam.yaw += dx * cam.turnSpeed;
    cam.pitch -= dy * cam.turnSpeed;

    if (cam.pitch > 89.f)  cam.pitch = 89.f;
    if (cam.pitch < -89.f) cam.pitch = -89.f;
}

void Application::runLoop()
{
    Camera camera(glm::vec3(8.0f, 8.0f, 30.0f));
    bool wireframeWasPressed = false;

    while (m_isRunning && !m_window->shouldClose()) {
        m_window->pollEvents();
        m_time->update();
        float dt = m_time->getDeltaTime();

        // 1) Handle camera input
        handleInput(camera, dt);

        // 2) Update chunks near the player's position
        if (m_voxelWorld) {
            m_voxelWorld->updateChunksAroundPlayer(camera.position.x, camera.position.z);
        }

        // 3) Press F => toggle wireframe
        bool wireframeIsPressed = (glfwGetKey(m_window->getGLFWwindow(), GLFW_KEY_F) == GLFW_PRESS);
        if (wireframeIsPressed && !wireframeWasPressed) {
            Logger::Info("Toggling wireframe mode...");
            m_renderer->toggleWireframe();
        }
        wireframeWasPressed = wireframeIsPressed;

        // 4) Render
        m_renderer->setCamera(camera);
        m_renderer->renderFrame();

        
    }
}

void Application::cleanup()
{
    // 1) Destroy VoxelWorld before Vulkan device
    if (m_voxelWorld) {
        delete m_voxelWorld;
        m_voxelWorld = nullptr;
    }

    // 2) Then renderer
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }

    // 3) Vulkan context
    if (m_vulkanCtx) {
        m_vulkanCtx->cleanup();
        delete m_vulkanCtx;
        m_vulkanCtx = nullptr;
    }

    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
    if (m_time) {
        delete m_time;
        m_time = nullptr;
    }
    m_isRunning = false;
}