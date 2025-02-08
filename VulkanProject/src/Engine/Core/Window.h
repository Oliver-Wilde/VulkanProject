#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <string>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Window
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    /**
     * Constructs a new window with the specified width, height, and title.
     *
     * @param width  The width of the window.
     * @param height The height of the window.
     * @param title  The title of the window.
     */
    Window(int width, int height, const std::string& title);

    /**
     * Destroys the window and terminates GLFW.
     */
    ~Window();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Checks if the window should close (e.g., if the user has requested to close it).
     *
     * @return true if the window should close, false otherwise.
     */
    bool shouldClose() const;

    /**
     * Polls for window events (keyboard, mouse, etc.).
     */
    void pollEvents();

    /**
     * Retrieves the underlying GLFWwindow handle.
     * This can be used by VulkanContext or other systems that need the GLFW window.
     *
     * @return Pointer to the GLFWwindow.
     */
    GLFWwindow* getGLFWwindow() const { return m_window; }

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    GLFWwindow* m_window = nullptr; ///< Pointer to the GLFW window
};
