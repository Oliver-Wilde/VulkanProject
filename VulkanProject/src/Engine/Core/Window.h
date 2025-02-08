#pragma once

#include <string>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Window
{
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void pollEvents();

    // Let VulkanContext create the surface
    // We provide a getter for the GLFWwindow*
    GLFWwindow* getGLFWwindow() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};