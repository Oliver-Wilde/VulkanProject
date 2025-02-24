// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "Window.h"
#include <stdexcept>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
Window::Window(int width, int height, const std::string& title)
{
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    // we use vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // create window
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        throw std::runtime_error("Failed to create GLFW window.");
    }
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents()
{
    glfwPollEvents();
}
