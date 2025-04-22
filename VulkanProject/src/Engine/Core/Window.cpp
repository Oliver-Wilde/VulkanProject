#include "Window.h"
#include <GLFW/glfw3.h>          // ← explicit include so this TU builds standalone
#include <stdexcept>
#include <iostream>              // optional for debug logs

Window::Window(int width, int height, const std::string& title)
{
    /* 1) Init GLFW -------------------------------------------------------- */
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW.");

    /* 2) Vulkan‑compatible window hints ----------------------------------- */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    /* 3) Create window ---------------------------------------------------- */
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("Failed to create GLFW window.");

    /* 4) Hook framebuffer‑resize callback --------------------------------- */
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

Window::~Window()
{
    if (m_window)
    {
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

/* ------------------------------------------------------------------------ */
/* static                                                                    */
/* ------------------------------------------------------------------------ */
void Window::framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) self->m_framebufferResized = true;
}
