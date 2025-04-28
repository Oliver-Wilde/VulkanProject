// ============================================================================
// VulkanContext.cpp   (FULL FILE — validation layers now auto?disabled in
//                      Release; no other functional changes)
// ============================================================================

#include "VulkanContext.h"
#include "Engine/Core/Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <vector>
#include <iostream>
#include <set>
#include <cstring>

// ---------------------------------------------------------------------------
// Debug?utils callback helpers
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT        messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
    std::cerr << "[Validation] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;    // do not abort Vulkan calls
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance                                instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return (func != nullptr)
        ? func(instance, pCreateInfo, pAllocator, pDebugMessenger)
        : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance                   instance,
    VkDebugUtilsMessengerEXT     messenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) func(instance, messenger, pAllocator);
}

// ============================================================================
// Public lifecycle
// ============================================================================
void VulkanContext::init(Window* window)
{
    createInstance();
    if (enableValidationLayers)            // constexpr from the header
        setupDebugMessenger();

    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

void VulkanContext::cleanup()
{
    if (enableValidationLayers && m_debugMessenger)
    {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_commandPool)
    {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_device)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Instance & validation layer setup
// ============================================================================
void VulkanContext::createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport())
        throw std::runtime_error("Validation layers requested, but not available!");

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MyVoxelEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "NoEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExt, glfwExt + glfwExtCount);
    if (enableValidationLayers)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan instance!");
}

void VulkanContext::createSurface(Window* window)
{
    if (glfwCreateWindowSurface(m_instance, window->getGLFWwindow(), nullptr, &m_surface)
        != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface!");
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (!count) throw std::runtime_error("No Vulkan?capable GPU found!");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    m_physicalDevice = devices[0];    // TODO: score & pick best
    if (m_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("Failed to choose a physical device!");
}

// ============================================================================
// Logical device + queues
// ============================================================================
void VulkanContext::createLogicalDevice()
{
    // 1. Queue family discovery -------------------------------------------------
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, qProps.data());

    int graphics = -1, present = -1;
    for (uint32_t i = 0; i < qCount; ++i)
    {
        if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = static_cast<int>(i);

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        if (presentSupport) present = static_cast<int>(i);

        if (graphics != -1 && present != -1) break;
    }
    if (graphics == -1 || present == -1)
        throw std::runtime_error("Required queue families missing!");

    m_graphicsFamilyIndex = static_cast<uint32_t>(graphics);

    // 2. Queue create infos -----------------------------------------------------
    float priority = 1.f;
    std::set<int> unique = { graphics, present };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (int fam : unique)
    {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    // 3. Requested core features ------------------------------------------------
    VkPhysicalDeviceFeatures feats{};
    feats.fillModeNonSolid = VK_TRUE;   // allow wire-frame mode

    // 4. Extensions -------------------------------------------------------------
    const std::vector<const char*> devExt = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME   // enable timeline semaphores
    };

    // 5. Timeline-semaphore feature struct -------------------------------------
    VkPhysicalDeviceTimelineSemaphoreFeatures tlFeat{};
    tlFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    tlFeat.pNext = nullptr;
    tlFeat.timelineSemaphore = VK_TRUE;

    // 6. Device-create info -----------------------------------------------------
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &tlFeat;                              // chain feature struct
    dci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    dci.pQueueCreateInfos = queueInfos.data();
    dci.pEnabledFeatures = &feats;
    dci.enabledExtensionCount = static_cast<uint32_t>(devExt.size());
    dci.ppEnabledExtensionNames = devExt.data();

    if (enableValidationLayers)
    {
        dci.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        dci.ppEnabledLayerNames = validationLayers.data();
    }

    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device!");

    // 7. Retrieve queues --------------------------------------------------------
    vkGetDeviceQueue(m_device, graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, present, 0, &m_presentQueue);

    // 8. Command pool -----------------------------------------------------------
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = m_graphicsFamilyIndex;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &pci, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool!");
}

// ============================================================================
// Validation helpers
// ============================================================================
bool VulkanContext::checkValidationLayerSupport()
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> props(count);
    vkEnumerateInstanceLayerProperties(&count, props.data());

    for (const char* name : validationLayers)
    {
        bool found = false;
        for (const auto& p : props)
            if (strcmp(name, p.layerName) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

void VulkanContext::setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    populateDebugMessengerCreateInfo(ci);

    if (CreateDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger)
        != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug messenger!");
}

void VulkanContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& ci)
{
    ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    ci.pUserData = nullptr;
}

// ============================================================================
// end of file
// ============================================================================
