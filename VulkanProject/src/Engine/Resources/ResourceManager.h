#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanContext;

class ResourceManager
{
public:
    ResourceManager(VulkanContext* context);
    ~ResourceManager();

    // Loads a SPIR-V shader file and returns a VkShaderModule.
    // Caches modules so repeated calls with the same path return the same module.
    VkShaderModule loadShaderModule(const std::string& filePath);

private:
    VulkanContext* m_context;

    // Cache of loaded shader modules
    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    // Helper to read a file into a byte buffer
    std::vector<char> readFile(const std::string& filePath);
};
