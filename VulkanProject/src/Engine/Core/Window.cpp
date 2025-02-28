#include "Window.h"
#include <stdexcept>
#include <iostream> // optional for debug logs

Window::Window(int width, int height, const std::string& title)
{
    // 1) Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    // 2) Set window hints for Vulkan usage + resizable window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // 3) Create window
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        throw std::runtime_error("Failed to create GLFW window.");
    }

    // 4) Store 'this' so we can set/clear m_framebufferResized in the static callback
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

Window::~Window()
{
    // Destroy window + terminate GLFW
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents()
{
    glfwPollEvents();
}

// -------------------------------------------------------
// STATIC CALLBACKS
// -------------------------------------------------------
void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    // Retrieve the Window instance user pointer
    auto w = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (w) {
        // Mark that we have a pending resize
        w->m_framebufferResized = true;

        // Optional: You might also log debug info
        // std::cout << "Framebuffer resized to " << width << " x " << height << "\n";
    }
}
