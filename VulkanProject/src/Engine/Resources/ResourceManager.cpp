// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"

#include <fstream>
#include <stdexcept>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
ResourceManager::ResourceManager(VulkanContext* context)
    : m_context(context)
{
}

ResourceManager::~ResourceManager()
{
    // Destroy all loaded shader modules
    for (auto& kv : m_shaderModules) {
        vkDestroyShaderModule(m_context->getDevice(), kv.second, nullptr);
    }
    m_shaderModules.clear();
}

// -----------------------------------------------------------------------------
// Private / Helper Methods
// -----------------------------------------------------------------------------
std::vector<char> ResourceManager::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
VkShaderModule ResourceManager::loadShaderModule(const std::string& filePath)
{
    // Check if this shader module is already loaded
    auto it = m_shaderModules.find(filePath);
    if (it != m_shaderModules.end()) {
        return it->second;
    }

    // Otherwise, load from disk
    auto code = readFile(filePath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule)
        != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module for " + filePath);
    }

    m_shaderModules[filePath] = shaderModule;
    return shaderModule;
}
