#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class VulkanContext;

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class ResourceManager
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    ResourceManager(VulkanContext* context);
    ~ResourceManager();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Loads a SPIR-V shader file and returns a VkShaderModule.
     * Caches modules so repeated calls with the same path return the same module.
     *
     * @param filePath The path to the SPIR-V shader file.
     * @return A valid VkShaderModule.
     */
    VkShaderModule loadShaderModule(const std::string& filePath);

private:
    // -----------------------------------------------------------------------------
    // Private Methods
    // -----------------------------------------------------------------------------
    /**
     * Reads a file from the given path into a byte buffer.
     *
     * @param filePath The path to the file.
     * @return A vector containing the file data.
     */
    std::vector<char> readFile(const std::string& filePath);

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;

    /**
     * Caches loaded shader modules to avoid re-loading the same file multiple times.
     * Key: File path, Value: VkShaderModule
     */
    std::unordered_map<std::string, VkShaderModule> m_shaderModules;
};
