#pragma once

class Window;
class Time;
class VulkanContext;
class Renderer;
class Camera;
class VoxelWorld;

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
    VulkanContext* m_vulkanCtx = nullptr;
    Renderer* m_renderer = nullptr;
    VoxelWorld* m_voxelWorld = nullptr; // Our voxel world pointer

    bool m_isRunning = false;
};