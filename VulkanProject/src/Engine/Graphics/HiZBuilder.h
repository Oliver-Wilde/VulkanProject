#ifndef HIZBUILDER_H
#define HIZBUILDER_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <fstream>

/**
 * Forward-declare your VulkanContext class
 * so we don't need to include many headers here.
 */
class VulkanContext;

/**
 * HiZInfo struct:
 *  - Describes the image array for your hierarchical Z data.
 *  - 'hiZImageView[m]' is the view for the 'm'-th mip level.
 *  - 'width' and 'height' are the full-res size of the base mip.
 *  - 'mipCount' is how many mips we have in total.
 *
 * Typically, you'd create a single VkImage with multiple mip levels,
 * and for each mip, you might create a separate VkImageView or
 * use a single view with subresourceRange.
 */
struct HiZInfo
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;

    // Each mip might have its own imageView, if you want to attach them
    // or you can store them in a single imageView if you do subresource
    // binding in your descriptors. For simplicity, we use per-mip views:
    std::vector<VkImageView> hiZImageView;
};

/**
 * HiZBuilder:
 *  - Builds a hierarchical Z buffer by downsampling
 *    each mip level from the previous one, using a compute pass.
 */
class HiZBuilder
{
public:
    /**
     * Construct with a pointer to your VulkanContext so we can
     * create pipelines, descriptors, etc.
     */
    explicit HiZBuilder(VulkanContext* context);

    /**
     * Destroy pipeline/descriptor resources, not the image itself.
     */
    ~HiZBuilder();

    /**
     * Freed if not done in destructor.
     */
    void cleanup();

    /**
     * Build the hierarchical Z data by dispatching compute for each mip
     * from 1..(mipCount - 1). Must be called after the base mip is populated
     * with the actual depth data.
     *
     * @param cmdBuf  A command buffer in the recording state (compute capable).
     * @param info    Info about the hi-z images, widths, mips, etc.
     */
    void buildHiZ(VkCommandBuffer cmdBuf, const HiZInfo& info);

private:
    // Our Vulkan context pointer, not owned
    VulkanContext* m_context = nullptr;

    // Pipeline and descriptor resources
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout      m_downsampleLayout = VK_NULL_HANDLE;
    VkPipeline            m_downsamplePipeline = VK_NULL_HANDLE;

private:
    /**
     * Create descriptor set layouts, pools, etc.
     */
    void createDescriptors();

    /**
     * Create the compute pipeline for downsample.
     */
    void createComputePipeline();

    /**
     * Utility: load a SPIR-V compute shader from file.
     */
    VkShaderModule loadShaderModule(const std::string& filePath);

    /**
     * Utility: read an entire file into a char vector.
     */
    std::vector<char> readFile(const std::string& filePath);
};

#endif // HIZBUILDER_H
