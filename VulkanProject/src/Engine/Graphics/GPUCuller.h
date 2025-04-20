#ifndef GPUCULLER_H
#define GPUCULLER_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

/**
 * Forward declare your VulkanContext so we don't need a big include here.
 */
class VulkanContext;

/**
 * GPUCullerInput:
 *
 * This struct is passed to runCulling() to let the compute shader know:
 *  - boundingVolumeBuffer: a buffer of AABBs or bounding volumes.
 *  - hiZView: the hierarchical Z image or top-level view we sample from.
 *  - drawCommandBuffer: a buffer of VkDrawIndexedIndirectCommand,
 *     which the compute shader will fill out if a chunk is visible.
 *  - drawCountBuffer: optional, if we want to store the # of visible draws
 *     (used for vkCmdDrawIndexedIndirectCountKHR).
 *
 * boundingVolumeCount is passed separately to the function, so we know how many volumes to process.
 */
struct GPUCullerInput
{
    VkBuffer    boundingVolumeBuffer = VK_NULL_HANDLE; // AABB array
    VkImageView hiZView = VK_NULL_HANDLE;              // The hi-z image

    VkBuffer    drawCommandBuffer = VK_NULL_HANDLE;    // We'll write the indirect draw commands here
    VkBuffer    drawCountBuffer = VK_NULL_HANDLE;      // Optional, if used
};

/**
 * GPUCuller:
 *  - Creates a compute pipeline for culling bounding volumes with a hierarchical Z
 *  - Maintains a single descriptor set (m_descSet). We "update" it each frame,
 *    rather than allocating a new one every time.
 */
class GPUCuller
{
public:
    explicit GPUCuller(VulkanContext* context);
    ~GPUCuller();

    /**
     * clean up pipeline, descriptor set layout, pool, etc.
     */
    void cleanup();

    /**
     * runCulling:
     *  - Updates our single descriptor set (m_descSet) with boundingVolumeBuffer,
     *    hiZView, drawCommandBuffer, and optional drawCountBuffer.
     *  - Dispatches the compute shader to cull bounding volumes and produce final indirect calls.
     *
     * @param cmdBuf: a command buffer in the recording state
     * @param input: see GPUCullerInput
     * @param boundingVolumeCount: how many bounding volumes to process
     */
    void runCulling(
        VkCommandBuffer cmdBuf,
        const GPUCullerInput& input,
        uint32_t boundingVolumeCount);

private:
    VulkanContext* m_context = nullptr;

    // descriptor set layout, pipeline layout, pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;

    VkPipelineLayout      m_cullPipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_cullPipeline = VK_NULL_HANDLE;

    // [NEW] Single descriptor set that we update each time:
    VkDescriptorSet       m_descSet = VK_NULL_HANDLE;

private:
    /**
     * Creates the descriptor set layout for:
     *   binding=0 => boundingVolume buffer
     *   binding=1 => hiZ image
     *   binding=2 => drawCommands buffer
     *   binding=3 => optional drawCount / uniform
     */
    void createDescriptorSetLayout();

    /**
     * Creates the compute pipeline from cull_hiz.comp.spv
     */
    void createComputePipeline();

    /**
     * Allocate exactly one descriptor set (m_descSet). We do this once at init,
     * then reuse the same set in runCulling() by calling vkUpdateDescriptorSets(...).
     */
    void allocateDescriptorSet();

    /**
     * Utility: load a SPIR-V file
     */
    VkShaderModule loadShaderModule(const std::string& filePath);

    /**
     * Utility: read file into char vector
     */
    std::vector<char> readFile(const std::string& filePath);
};

struct BoundingVolume
{
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

#endif // GPUCULLER_H
