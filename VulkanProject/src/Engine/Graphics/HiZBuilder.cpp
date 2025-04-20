#include "Engine/Graphics/HiZBuilder.h"
#include "Engine/Graphics/VulkanContext.h"
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iostream>

/**
 * HiZBuilder is responsible for:
 *  1) Creating a storage image (the "hierarchical Z" pyramid).
 *  2) Providing a compute pipeline that down-samples the depth
 *     from full-res to half-res, half-res to quarter-res, etc.
 *  3) Exposing a buildHiZ() method that the main rendering flow calls
 *     after the depth pass is done, but before culling.
 *
 * We have updated descriptor pool sizing and added a check for null image views.
 */

static const char* HIZ_DOWN_SAMPLE_COMP_SPV = "shaders/hiz_downsample.comp.spv";

HiZBuilder::HiZBuilder(VulkanContext* context)
    : m_context(context)
{
    createDescriptors();
    createComputePipeline();
}

HiZBuilder::~HiZBuilder()
{
    cleanup();
}

void HiZBuilder::cleanup()
{
    VkDevice device = m_context->getDevice();

    // Destroy pipeline + layout
    if (m_downsamplePipeline) {
        vkDestroyPipeline(device, m_downsamplePipeline, nullptr);
        m_downsamplePipeline = VK_NULL_HANDLE;
    }
    if (m_downsampleLayout) {
        vkDestroyPipelineLayout(device, m_downsampleLayout, nullptr);
        m_downsampleLayout = VK_NULL_HANDLE;
    }

    // Destroy descriptor pool
    if (m_descriptorPool) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (m_descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    // We do NOT destroy the hi-z image here if we assume the user
    // manages it externally. If we own it, do so here.
}

void HiZBuilder::createDescriptors()
{
    /**
     * We'll need something like:
     *  1) A storage image binding for the current mip
     *  2) A storage image binding for the previous mip
     *
     * We also create a bigger descriptor pool so we don't run out.
     */
    VkDevice device = m_context->getDevice();

    // (1) Two bindings: source (mip-1), dest (mip)
    VkDescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("HiZBuilder: Failed to create descriptor set layout!");
    }

    // (2) Create a descriptor pool with more capacity
    // We often allocate 2 storage images (src + dst) for each mip. If you have many mips, 
    // you need plenty of capacity.
    VkDescriptorPoolSize poolSizes[1]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 5000000; // bigger to avoid running out

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 500000;  // enough sets for all your mips
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("HiZBuilder: Failed to create descriptor pool!");
    }
}

void HiZBuilder::createComputePipeline()
{
    /**
     * We'll create a pipeline that runs hiz_downsample.comp.spv.
     * If you don't have push constants for HiZ, no need to add them here.
     */
    VkDevice device = m_context->getDevice();

    // 1) Load the SPIR-V module
    VkShaderModule shaderModule = loadShaderModule(HIZ_DOWN_SAMPLE_COMP_SPV);

    // 2) Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 0; // if not using push constants
    layoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_downsampleLayout) != VK_SUCCESS) {
        throw std::runtime_error("HiZBuilder: Failed to create pipeline layout for HiZ!");
    }

    // 3) Create compute pipeline
    VkComputePipelineCreateInfo compInfo{};
    compInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compInfo.stage.module = shaderModule;
    compInfo.stage.pName = "main";
    compInfo.layout = m_downsampleLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compInfo, nullptr, &m_downsamplePipeline) != VK_SUCCESS) {
        throw std::runtime_error("HiZBuilder: Failed to create compute pipeline for HiZ!");
    }

    // 4) Destroy shader module
    vkDestroyShaderModule(device, shaderModule, nullptr);
}

VkShaderModule HiZBuilder::loadShaderModule(const std::string& filePath)
{
    auto code = readFile(filePath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module for HiZBuilder!");
    }
    return shaderModule;
}

std::vector<char> HiZBuilder::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

void HiZBuilder::buildHiZ(VkCommandBuffer cmdBuf, const HiZInfo& info)
{
    /**
     * This is where we do the actual down-sampling for each mip.
     * We must avoid writing descriptors with VK_NULL_HANDLE for imageViews.
     */
    VkDevice device = m_context->getDevice();

    // If the user passed empty hiZImageView vector, skip to avoid null descriptors
    if (info.hiZImageView.empty()) {
        // no views => skip
        return;
    }

    // For each mip from 1..(info.mipCount-1):
    for (uint32_t mip = 1; mip < info.mipCount; mip++)
    {
        // sanity check
        if (info.hiZImageView[mip] == VK_NULL_HANDLE ||
            info.hiZImageView[mip - 1] == VK_NULL_HANDLE)
        {
            // skip this mip level if we have no valid view
            continue;
        }

        // 1) allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        VkDescriptorSet descSet;
        if (vkAllocateDescriptorSets(device, &allocInfo, &descSet) != VK_SUCCESS) {
            throw std::runtime_error("HiZBuilder: Failed to allocate desc set for mip!");
        }

        // 2) Source is mip-1
        VkDescriptorImageInfo srcInfo{};
        srcInfo.sampler = VK_NULL_HANDLE;
        srcInfo.imageView = info.hiZImageView[mip - 1];
        srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // or SHADER_READ_ONLY_OPTIMAL, if that’s how you use it

        // 3) Dest is this mip
        VkDescriptorImageInfo dstInfo{};
        dstInfo.sampler = VK_NULL_HANDLE;
        dstInfo.imageView = info.hiZImageView[mip];
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // 4) Write them
        VkWriteDescriptorSet writes[2]{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSet;
        writes[0].dstBinding = 0; // source
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &srcInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSet;
        writes[1].dstBinding = 1; // destination
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &dstInfo;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

        // 5) Bind pipeline + desc
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);
        vkCmdBindDescriptorSets(cmdBuf,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            m_downsampleLayout,
            0, 1, &descSet,
            0, nullptr);

        // 6) dispatch
        // Typically you do (width >> mip), (height >> mip) for group counts, 
        // possibly dividing by your local size. For example:
        uint32_t localSize = 8; // depends on your comp shader
        uint32_t dispatchW = (std::max)(1u, (info.width >> mip) / localSize);
        uint32_t dispatchH = (std::max)(1u, (info.height >> mip) / localSize);
        vkCmdDispatch(cmdBuf, dispatchW, dispatchH, 1);
    }
}
