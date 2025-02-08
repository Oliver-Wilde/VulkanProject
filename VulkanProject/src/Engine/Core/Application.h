#pragma once

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class Window;
class Time;
class VulkanContext;
class Renderer;
class Camera;
class VoxelWorld;

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Application
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    Application();
    ~Application();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Initializes all necessary components for the application (window,
     * Vulkan context, voxel world, renderer, etc.).
     */
    void init();

    /**
     * Starts the main loop, processing events, updating, and rendering frames.
     */
    void runLoop();

    /**
     * Cleans up resources before closing the application.
     */
    void cleanup();

private:
    // -----------------------------------------------------------------------------
    // Private Methods
    // -----------------------------------------------------------------------------
    /**
     * Handles user input (keyboard, mouse) to update the camera state.
     *
     * @param cam Reference to the camera to be updated.
     * @param dt Delta time since last frame.
     */
    void handleInput(Camera& cam, float dt);

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    Window* m_window = nullptr;  ///< Pointer to the window
    Time* m_time = nullptr;  ///< Pointer to the time/delta-time manager
    VulkanContext* m_vulkanCtx = nullptr;  ///< Pointer to the Vulkan context
    Renderer* m_renderer = nullptr;  ///< Pointer to the renderer
    VoxelWorld* m_voxelWorld = nullptr;  ///< Pointer to the voxel world

    bool           m_isRunning = false;    ///< Flag to keep the main loop running
};
