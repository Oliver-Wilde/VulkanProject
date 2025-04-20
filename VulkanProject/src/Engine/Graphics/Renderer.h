#pragma once

#include <deque>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>

#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"   // QueuedChunkDestruction

// ───────────────────────── forward declarations ────────────────────────────
class Window;
class VulkanContext;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Camera;
class Time;
class UIRenderer;

// ───────────────────────── MVP uniform payload ─────────────────────────────
struct MVPBlock
{
    glm::mat4 mvp;
};

// ───────────────────────── per‑flight frame resources ──────────────────────
struct FrameResources
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

static const int MAX_FRAMES_IN_FLIGHT = 2;   // double‑buffered

// ═══════════════════════════   R E N D E R E R   ═══════════════════════════
class Renderer
{
public:
    Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld);
    ~Renderer();

    void renderFrame();
    void setCamera(const Camera& cam);
    void toggleWireframe();
    void enableFrustumCulling(bool enable);
    void setTime(Time* time);
    void enqueueDeferredDestroy(const QueuedChunkDestruction& qcd);

private:
    // ───────────────────────‑ big‑buffer “MeshBatch” ‑──────────────────────
    struct MeshBatch
    {
        // GPU resources
        VkBuffer       vbo = VK_NULL_HANDLE;
        VkBuffer       ibo = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;   // vertex allocation

        // total bytes currently allocated
        VkDeviceSize   vboSize = 0;
        VkDeviceSize   iboSize = 0;

        // bytes consumed during *this* frame
        VkDeviceSize   vboUsed = 0;
        VkDeviceSize   iboUsed = 0;

        /** Ensure the big buffers have at least the requested capacity.
         *  May destroy & recreate buffers if they need to grow. */
        void ensureCapacity(Renderer* owner,
            VkDeviceSize wantVbo,
            VkDeviceSize wantIbo);

        /** Reset usage counters at frame start. */
        inline void reset() { vboUsed = iboUsed = 0; }

        /** Append one chunk’s geometry into the big VBO/IBO.
         *  Returns the *first index* for that chunk. */
        uint32_t appendChunk(Renderer* owner,
            VkBuffer         srcVbo, VkDeviceSize srcVboBytes,
            VkBuffer         srcIbo, VkDeviceSize srcIboBytes);
    };

    // helper that builds current frame’s batch & records one draw call
    void buildAndRecordBatch(VkCommandBuffer cmdBuf);

    // ───────────────────── internal helpers ────────────────────────────────
    void createMVPUniformBuffer();
    void updateMVP();
    void recreateSwapChain();

    void createBuffer(VkDeviceSize         size,
        VkBufferUsageFlags   usage,
        VkMemoryPropertyFlags props,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
    void     addSample(std::deque<float>& buffer, float value);
    float    computeAverage(const std::deque<float>& buffer);
    void     freeDeferredResources();

    // ───────────────────── member variables ────────────────────────────────
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;
    UIRenderer* m_uiRenderer = nullptr;

    class SwapChain* m_swapChain = nullptr;

    // MVP uniform
    VkBuffer            m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool    m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet     m_mvpDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;

    // per‑frame chunk‑buffer destroy queue
    std::vector<QueuedChunkDestruction> m_deferredFrees[MAX_FRAMES_IN_FLIGHT];

    // frame resources
    FrameResources      m_frames[MAX_FRAMES_IN_FLIGHT];
    int                 m_currentFrame = 0;

    // per‑frame big‑buffer batches
    MeshBatch           m_batches[MAX_FRAMES_IN_FLIGHT];

    // timing & stats
    Time* m_time = nullptr;
    bool                m_wireframeOn = false;
    bool                m_enableFrustumCulling = false;
    static const int    ROLLING_AVG_SAMPLES = 120;
    std::deque<float>   m_fpsSamples;
    std::deque<float>   m_cpuSamples;

    // camera
    Camera              m_camera;
};
