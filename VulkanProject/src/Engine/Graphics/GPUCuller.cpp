#include "GPUCuller.h"
#include "Engine/Graphics/VulkanContext.h"
#include <stdexcept>
#include <fstream>
#include <cmath>

// Example push constant data (only if your cull shader uses them)
struct CullerPushConstants
{
    float someValue;
};

static const char* CULL_HIZ_COMP_SPV = "shaders/cull_hiz.comp.spv";

GPUCuller::GPUCuller(VulkanContext* context)
    : m_context(context)
{
    // 1) Create layout + pipeline
    createDescriptorSetLayout();
    createComputePipeline();

    // 2) Allocate exactly one descriptor set now
    allocateDescriptorSet();
}

GPUCuller::~GPUCuller()
{
    cleanup();
}

void GPUCuller::cleanup()
{
    VkDevice device = m_context->getDevice();

    // If you want to free the single descriptor set:
    // if (m_descSet != VK_NULL_HANDLE) {
    //     vkFreeDescriptorSets(device, m_descriptorPool, 1, &m_descSet);
    //     m_descSet = VK_NULL_HANDLE;
    // }

    if (m_cullPipeline) {
        vkDestroyPipeline(device, m_cullPipeline, nullptr);
        m_cullPipeline = VK_NULL_HANDLE;
    }
    if (m_cullPipelineLayout) {
        vkDestroyPipelineLayout(device, m_cullPipelineLayout, nullptr);
        m_cullPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorPool) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

void GPUCuller::createDescriptorSetLayout()
{
    VkDevice device = m_context->getDevice();

    // We have 4 bindings (0=boundingVolume buffer, 1=hiZ image, 2=drawCommands, 3=drawCount optional)
    VkDescriptorSetLayoutBinding bindings[4]{};

    // 0) boundingVolume
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1) hiZ image
    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2) drawCommands
    bindings[2].binding = 2;
    bindings[2].descriptorCount = 1;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3) optional drawCount
    bindings[3].binding = 3;
    bindings[3].descriptorCount = 1;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to create descriptor set layout!");
    }

    // 2) Make descriptor pool with enough capacity for 1 or a few sets
    // Because we only do one set, we just need ~4 descriptors total. Let's be safe:
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 10; // enough for multiple buffer usage
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 10; // enough for the hiZ usage

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // We'll only create e.g. 2 sets total if you want a second culler, but 1 is enough
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to create culler descriptor pool!");
    }
}

void GPUCuller::allocateDescriptorSet()
{
    if (m_descSet != VK_NULL_HANDLE) {
        // Already allocated; skip. 
        return;
    }

    VkDevice device = m_context->getDevice();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descSet) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to allocate cull descriptor set!");
    }
}

void GPUCuller::createComputePipeline()
{
    VkDevice device = m_context->getDevice();

    // load compute shader
    VkShaderModule module = loadShaderModule(CULL_HIZ_COMP_SPV);

    // push constant range if used
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(CullerPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1; // if using push constants
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_cullPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to create cull pipeline layout!");
    }

    // create compute pipeline
    VkComputePipelineCreateInfo compInfo{};
    compInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compInfo.stage.module = module;
    compInfo.stage.pName = "main";
    compInfo.layout = m_cullPipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compInfo, nullptr, &m_cullPipeline) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to create cull pipeline!");
    }

    vkDestroyShaderModule(device, module, nullptr);
}

VkShaderModule GPUCuller::loadShaderModule(const std::string& filePath)
{
    auto code = readFile(filePath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("GPUCuller: Failed to create cull shader module!");
    }
    return shaderModule;
}

std::vector<char> GPUCuller::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("GPUCuller: Failed to open file: " + filePath);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

/**
 * runCulling => Now we simply "update" the single descriptor set (m_descSet).
 * No new set is allocated each frame => no risk of descriptor pool exhaustion.
 */
void GPUCuller::runCulling(
    VkCommandBuffer cmdBuf,
    const GPUCullerInput& input,
    uint32_t boundingVolumeCount)
{
    // If user tries to run culling but no resources => skip
    if (input.hiZView == VK_NULL_HANDLE ||
        input.boundingVolumeBuffer == VK_NULL_HANDLE ||
        input.drawCommandBuffer == VK_NULL_HANDLE)
    {
        return;
    }

    VkDevice device = m_context->getDevice();

    // 1) Build the descriptor writes for m_descSet
    std::vector<VkWriteDescriptorSet> writes;

    // bounding volumes => binding=0
    VkDescriptorBufferInfo bvolInfo{};
    bvolInfo.buffer = input.boundingVolumeBuffer;
    bvolInfo.offset = 0;
    bvolInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet w0{};
    w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.dstSet = m_descSet;
    w0.dstBinding = 0;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w0.descriptorCount = 1;
    w0.pBufferInfo = &bvolInfo;
    writes.push_back(w0);

    // hiZ => binding=1
    VkDescriptorImageInfo hizInfo{};
    hizInfo.sampler = VK_NULL_HANDLE;
    hizInfo.imageView = input.hiZView;
    hizInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet w1{};
    w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1.dstSet = m_descSet;
    w1.dstBinding = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w1.descriptorCount = 1;
    w1.pImageInfo = &hizInfo;
    writes.push_back(w1);

    // drawCommands => binding=2
    VkDescriptorBufferInfo drawCmdInfo{};
    drawCmdInfo.buffer = input.drawCommandBuffer;
    drawCmdInfo.offset = 0;
    drawCmdInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet w2{};
    w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w2.dstSet = m_descSet;
    w2.dstBinding = 2;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w2.descriptorCount = 1;
    w2.pBufferInfo = &drawCmdInfo;
    writes.push_back(w2);

    // optional drawCount => binding=3
    if (input.drawCountBuffer != VK_NULL_HANDLE)
    {
        VkDescriptorBufferInfo countInfo{};
        countInfo.buffer = input.drawCountBuffer;
        countInfo.offset = 0;
        countInfo.range = sizeof(uint32_t); // or bigger if storing more data

        VkWriteDescriptorSet w3{};
        w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w3.dstSet = m_descSet;
        w3.dstBinding = 3;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w3.descriptorCount = 1;
        w3.pBufferInfo = &countInfo;
        writes.push_back(w3);
    }

    // 2) Update
    vkUpdateDescriptorSets(
        device,
        static_cast<uint32_t>(writes.size()),
        writes.data(),
        0, nullptr
    );

    // 3) Bind pipeline
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);

    // 4) Bind descriptor set
    vkCmdBindDescriptorSets(
        cmdBuf,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_cullPipelineLayout,
        0, // first set
        1, &m_descSet,
        0, nullptr
    );

    // optional push constants
    CullerPushConstants pc{};
    pc.someValue = 42.0f;
    vkCmdPushConstants(cmdBuf,
        m_cullPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc
    );

    // 5) Dispatch
    // e.g. group size = 64 => #groups for boundingVolumeCount
    uint32_t groupSize = 64;
    uint32_t dispatchCount = (boundingVolumeCount + groupSize - 1) / groupSize;
    vkCmdDispatch(cmdBuf, dispatchCount, 1, 1);
}