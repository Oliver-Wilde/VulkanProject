#pragma once

#include <string>

// Forward declarations
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

    void init();
    void runLoop();
    void cleanup();

private:
    void handleInput(Camera& cam, float dt);

private:
    Window* m_window = nullptr;
    Time* m_time = nullptr;
    VulkanContext* m_vulkanCtx = nullptr;  // Notice name
    Renderer* m_renderer = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;
    ResourceManager* m_resourceManager = nullptr;  // We'll allocate it in init()

    bool m_isRunning = false;
};
