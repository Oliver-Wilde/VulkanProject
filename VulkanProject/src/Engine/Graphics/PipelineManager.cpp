// ============================================================================
// PipelineManager.cpp  – updated 2025-04-28
//   • Switches voxel pipelines to *voxel_lit* shaders
//   • Adopts new 20-byte Vertex stride (position + color + 4-byte padding)
//   • Adds 16-byte push-constant range (directional light vector)
// ============================================================================
#include "PipelineManager.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Voxels/Meshing/IMesher.h"

#include <stdexcept>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create pipeline layout with optional descriptor set and push const
// ─────────────────────────────────────────────────────────────────────────────
static VkPipelineLayout createPipelineLayout(VulkanContext* ctx,
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE)
{
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = 16;         // vec3 + pad (std140)

    VkPipelineLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (dsl != VK_NULL_HANDLE)
    {
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &dsl;
    }
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pcRange;

    VkPipelineLayout pl{};
    if (vkCreatePipelineLayout(ctx->getDevice(), &ci, nullptr, &pl) != VK_SUCCESS)
        throw std::runtime_error("PipelineManager: pipeline-layout create failed");
    return pl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
PipelineManager::PipelineManager(VulkanContext* c, ResourceManager* rm)
    : m_context(c), m_resourceMgr(rm) {}

PipelineManager::~PipelineManager()
{
    for (auto& kv : m_pipelines)
    {
        vkDestroyPipeline(m_context->getDevice(), kv.second.pipeline, nullptr);
        vkDestroyPipelineLayout(m_context->getDevice(), kv.second.pipelineLayout, nullptr);
    }
    m_pipelines.clear();
}

// ============================================================================
// 1. createVoxelPipeline  (NO descriptor set)  – fill mode
// ============================================================================
void PipelineManager::createVoxelPipeline(const std::string& name,
    VkRenderPass    rp,
    VkExtent2D      extent)
{
    VkShaderModule vert = m_resourceMgr->loadShaderModule("shaders/voxel_lit.vert.spv");
    VkShaderModule frag = m_resourceMgr->loadShaderModule("shaders/voxel_lit.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    // vertex format: 20-byte stride, pos+color only (light bytes unused in GPU)
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr[2]{};
    // position
    attr[0].binding = 0;
    attr[0].location = 0;
    attr[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr[0].offset = 0;
    // color
    attr[1].binding = 0;
    attr[1].location = 1;
    attr[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attr[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attr;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;

    VkRect2D sc{ {0,0}, extent };

    VkPipelineViewportStateCreateInfo vpState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpState.viewportCount = 1; vpState.pViewports = &vp;
    vpState.scissorCount = 1; vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineLayout layout = createPipelineLayout(m_context);

    VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vpState;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = layout;
    pi.renderPass = rp;

    VkPipeline pipeline{};
    if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
        &pi, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("PipelineManager: voxel pipeline create failed");

    m_pipelines[name] = { pipeline, layout };
}

// ============================================================================
// 2. createVoxelPipelineFill  (descriptor set 0 = MVP)
// ============================================================================
void PipelineManager::createVoxelPipelineFill(const std::string& name,
    VkRenderPass    rp,
    VkExtent2D      extent,
    VkDescriptorSetLayout dsl)
{
    VkShaderModule vert = m_resourceMgr->loadShaderModule("shaders/voxel_lit.vert.spv");
    VkShaderModule frag = m_resourceMgr->loadShaderModule("shaders/voxel_lit.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr[2]{};
    attr[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    attr[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,  offsetof(Vertex, color) };

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attr;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{}; vp.width = float(extent.width); vp.height = float(extent.height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    VkRect2D sc{ {0,0}, extent };

    VkPipelineViewportStateCreateInfo vpState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpState.viewportCount = 1; vpState.pViewports = &vp;
    vpState.scissorCount = 1; vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineLayout layout = createPipelineLayout(m_context, dsl);

    VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vpState;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = layout;
    pi.renderPass = rp;

    VkPipeline pipeline{};
    if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
        &pi, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("PipelineManager: voxel FILL pipeline failed");

    m_pipelines[name] = { pipeline, layout };
}

// ============================================================================
// 3. createVoxelPipelineWireframe  (descriptor set 0 = MVP)
// ============================================================================
void PipelineManager::createVoxelPipelineWireframe(const std::string& name,
    VkRenderPass rp,
    VkExtent2D   extent,
    VkDescriptorSetLayout dsl)
{
    /* identical to FILL variant except for polygonMode = LINE */
    VkShaderModule vert = m_resourceMgr->loadShaderModule("shaders/voxel_lit.vert.spv");
    VkShaderModule frag = m_resourceMgr->loadShaderModule("shaders/voxel_lit.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    VkVertexInputBindingDescription bind{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attr[2]{};
    attr[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    attr[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,  offsetof(Vertex, color) };

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attr;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{}; vp.width = float(extent.width); vp.height = float(extent.height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    VkRect2D sc{ {0,0}, extent };

    VkPipelineViewportStateCreateInfo vpState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpState.viewportCount = 1; vpState.pViewports = &vp;
    vpState.scissorCount = 1; vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_LINE;        // wireframe
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineLayout layout = createPipelineLayout(m_context, dsl);

    VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vpState;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = layout;
    pi.renderPass = rp;

    VkPipeline pipeline{};
    if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
        &pi, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("PipelineManager: voxel WIREFRAME pipeline failed");

    m_pipelines[name] = { pipeline, layout };
}

// ============================================================================
// 4. createMVPDescriptorSetLayout  (unchanged)
// ============================================================================
VkDescriptorSetLayout PipelineManager::createMVPDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = 1;
    ci.pBindings = &ubo;

    VkDescriptorSetLayout dsl{};
    if (vkCreateDescriptorSetLayout(m_context->getDevice(), &ci, nullptr, &dsl) != VK_SUCCESS)
        throw std::runtime_error("PipelineManager: MVP descriptor-set layout failed");
    return dsl;
}

// ============================================================================
// 5. getPipeline
// ============================================================================
PipelineInfo PipelineManager::getPipeline(const std::string& name)
{
    auto it = m_pipelines.find(name);
    if (it == m_pipelines.end())
        throw std::runtime_error("PipelineManager: pipeline not found: " + name);
    return it->second;
}

