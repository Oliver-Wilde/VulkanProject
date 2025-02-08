#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------------
// Structs & Forward Declarations
// -----------------------------------------------------------------------------

// This structure holds both the pipeline and its layout.
struct PipelineInfo {
    VkPipeline       pipeline = VK_NULL_HANDLE;      // Vulkan pipeline handle
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; // Vulkan pipeline layout handle
};

class VulkanContext;
class ResourceManager;

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class PipelineManager {
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    PipelineManager(VulkanContext* context, ResourceManager* resourceMgr);
    ~PipelineManager();

    // -----------------------------------------------------------------------------
    // 1) No-descriptor pipeline (fill mode)
    //
    //    This pipeline doesn't require a descriptor set layout.
    //    It can be used if you do not need to bind uniform buffers
    //    or other resources.
    // -----------------------------------------------------------------------------
    void createVoxelPipeline(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent);

    // -----------------------------------------------------------------------------
    // 2) "Fill" pipeline WITH descriptor layout
    //
    //    This pipeline supports a descriptor set for passing uniforms/textures.
    // -----------------------------------------------------------------------------
    void createVoxelPipelineFill(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent,
        VkDescriptorSetLayout descriptorLayout);

    // -----------------------------------------------------------------------------
    // 3) "Wireframe" pipeline WITH descriptor layout
    //
    //    Similar to the fill pipeline, but renders in wireframe mode.
    // -----------------------------------------------------------------------------
    void createVoxelPipelineWireframe(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent,
        VkDescriptorSetLayout descriptorLayout);

    // -----------------------------------------------------------------------------
    // Create a descriptor set layout for the MVP uniform
    // -----------------------------------------------------------------------------
    VkDescriptorSetLayout createMVPDescriptorSetLayout();

    // -----------------------------------------------------------------------------
    // Retrieve a previously created pipeline by name
    // -----------------------------------------------------------------------------
    PipelineInfo getPipeline(const std::string& pipelineName);

private:
    // -----------------------------------------------------------------------------
    // Create an empty pipeline layout (no descriptor sets bound)
    // -----------------------------------------------------------------------------
    VkPipelineLayout createEmptyPipelineLayout();

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;                           // Vulkan context pointer
    ResourceManager* m_resourceMgr = nullptr;                     // Resource manager pointer
    std::unordered_map<std::string, PipelineInfo> m_pipelines;    // Map of pipeline name -> PipelineInfo
};
