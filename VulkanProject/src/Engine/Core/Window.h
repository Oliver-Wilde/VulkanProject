#pragma once

#include <string>
#include <GLFW/glfw3.h>

class Window
{
public:
    /**
     * Creates a GLFW window of the specified width and height,
     * with the given title. Also sets up Vulkan-related window hints.
     */
    Window(int width, int height, const std::string& title);
    ~Window();

    /**
     * @return true if the window should close (e.g., user pressed the X button).
     */
    bool shouldClose() const;

    /**
     * Polls for and processes events (e.g., keyboard, mouse, resize).
     */
    void pollEvents();

    /**
     * @return The underlying GLFWwindow* pointer (useful for Vulkan surface creation).
     */
    GLFWwindow* getGLFWwindow() const { return m_window; }

    /**
     * @return true if the framebuffer was resized since last check.
     */
    bool wasResized() const { return m_framebufferResized; }

    /**
     * Resets the 'wasResized' flag to false.
     * Call this after you handle a resize event.
     */
    void resetResizedFlag() { m_framebufferResized = false; }

private:
    /**
     * This static callback is used by GLFW to inform us when the framebuffer size changes.
     * We store that info in m_framebufferResized so the rest of the code can react.
     */
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* m_window = nullptr;

    // Tracks whether the framebuffer was resized this frame.
    bool m_framebufferResized = false;
};
