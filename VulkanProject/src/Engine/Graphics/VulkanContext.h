#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class Window; // We don't include the header here; we just forward-declare.

// -----------------------------------------------------------------------------
// Validation Layers / Debug Definitions
// -----------------------------------------------------------------------------
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// Our single validation layer
static const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// -----------------------------------------------------------------------------
// Function Declarations (Debug Utilities)
// -----------------------------------------------------------------------------
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger);

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator);

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class VulkanContext
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    VulkanContext() = default;
    ~VulkanContext() = default;

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Initializes the Vulkan context with a given window.
     * Creates the instance, surface, picks a physical device,
     * creates the logical device, etc.
     */
    void init(Window* window);

    /**
     * Cleans up all resources owned by VulkanContext.
     */
    void cleanup();

    // -----------------------------------------------------------------------------
    // Getters
    // -----------------------------------------------------------------------------
    VkInstance       getInstance()       const { return m_instance; }
    VkDevice         getDevice()         const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkSurfaceKHR     getSurface()        const { return m_surface; }
    VkQueue          getGraphicsQueue()  const { return m_graphicsQueue; }
    VkQueue          getPresentQueue()   const { return m_presentQueue; }

    /**
     * Gets the Vulkan command pool.
     */
    VkCommandPool getCommandPool() const { return m_commandPool; }

    /**
     * Sets the Vulkan command pool.
     */
    void setCommandPool(VkCommandPool pool) { m_commandPool = pool; }

    /**
     * @return The graphics queue family index.
     */
    uint32_t getGraphicsQueueFamilyIndex() const { return m_graphicsFamilyIndex; }

private:
    // -----------------------------------------------------------------------------
    // Private Methods (Initialization Steps)
    // -----------------------------------------------------------------------------
    void createInstance();
    void createSurface(Window* window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    // -----------------------------------------------------------------------------
    // Validation Layer Helpers
    // -----------------------------------------------------------------------------
    bool checkValidationLayerSupport();
    void setupDebugMessenger();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VkInstance              m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR            m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice        m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                m_device = VK_NULL_HANDLE;
    VkQueue                 m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue                 m_presentQueue = VK_NULL_HANDLE;
    VkCommandPool           m_commandPool = VK_NULL_HANDLE;
    uint32_t                m_graphicsFamilyIndex = 0;

    // Debug
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
};
