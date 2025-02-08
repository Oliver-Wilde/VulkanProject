#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class Window; // forward declaration

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// Our single validation layer
static const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// A small function pointer for the debug messenger
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger);

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator);

class VulkanContext
{
public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    void init(Window* window);
    void cleanup();

    // Existing getters
    VkInstance       getInstance()       const { return m_instance; }
    VkDevice         getDevice()         const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkSurfaceKHR     getSurface()        const { return m_surface; }
    VkQueue          getGraphicsQueue()  const { return m_graphicsQueue; }
    VkQueue          getPresentQueue()   const { return m_presentQueue; }

    // For command pool & queue family
    VkCommandPool getCommandPool() const { return m_commandPool; }
    void setCommandPool(VkCommandPool pool) { m_commandPool = pool; }
    uint32_t getGraphicsQueueFamilyIndex() const { return m_graphicsFamilyIndex; }

private:
    // Internal creation methods
    void createInstance();
    void createSurface(Window* window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    // Validation layer helpers
    bool checkValidationLayerSupport();
    void setupDebugMessenger(); // create the debug utils messenger
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

private:
    VkInstance       m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue          m_presentQueue = VK_NULL_HANDLE;

    // Command pool owned by VulkanContext
    VkCommandPool    m_commandPool = VK_NULL_HANDLE;

    // We'll remember which queue family is used for graphics
    uint32_t         m_graphicsFamilyIndex = 0;

    // Debug messenger
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
};
