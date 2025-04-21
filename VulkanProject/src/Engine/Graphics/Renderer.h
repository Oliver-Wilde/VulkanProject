#pragma once
// ───────────────────────────────────────────────────────────────────────────
// Renderer.h – batch‑rendering voxel renderer (no feature‑flags)
// ───────────────────────────────────────────────────────────────────────────
#include <deque>
#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>

#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"   // for QueuedChunkDestruction
#include <numeric>

// ─── forward declarations ─────────────────────────────────────────────────
class Window;
class VulkanContext;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Time;
class UIRenderer;
class Frustum;
class SwapChain;

// ─── uniform payload ──────────────────────────────────────────────────────
struct MVPBlock { glm::mat4 mvp; };

// ─── per‑flight resources ─────────────────────────────────────────────────
struct FrameResources
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

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
    // ─── big‑buffer batch helper ───────────────────────────────────────────
    struct MeshBatch
    {
        VkBuffer       vbo = VK_NULL_HANDLE;
        VkBuffer       ibo = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;           // vertex alloc
        VkDeviceSize   vboSize = 0, iboSize = 0;
        VkDeviceSize   vboUsed = 0, iboUsed = 0;

        void     ensureCapacity(Renderer* owner,
            VkDeviceSize wantVbo,
            VkDeviceSize wantIbo);
        inline void reset() { vboUsed = iboUsed = 0; }

        uint32_t appendChunk(Renderer* owner,
            VkBuffer srcVbo, VkDeviceSize srcVboBytes,
            VkBuffer srcIbo, VkDeviceSize srcIboBytes);
    };

    // ─── secondary geometry CB builder ────────────────────────────────────
    uint64_t buildGeometryCB(uint32_t imgIdx,
        const Frustum& fr,
        bool useCull,
        uint32_t& outVerts,
        uint32_t& outCalls);

    // ─── internal helpers (implemented in .cpp) ────────────────────────────
    void freeDeferredResources();
    void addSample(std::deque<float>& buf, float v);
    float computeAverage(const std::deque<float>& buf);

    void createMVPUniformBuffer();
    void updateMVP();
    void recreateSwapChain();

    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer& buffer,
        VkDeviceMemory& memory);

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    // ─── members ──────────────────────────────────────────────────────────
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;
    UIRenderer* m_uiRenderer = nullptr;
    SwapChain* m_swapChain = nullptr;

    // MVP uniform
    VkBuffer               m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory         m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool       m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet        m_mvpDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_mvpLayout = VK_NULL_HANDLE;

    // deferred GPU frees
    std::vector<QueuedChunkDestruction> m_deferredFrees[MAX_FRAMES_IN_FLIGHT];

    // per‑flight primary resources
    FrameResources m_frames[MAX_FRAMES_IN_FLIGHT];
    int            m_currentFrame = 0;

    // mesh‑batch (one per flight frame)
    MeshBatch      m_batches[MAX_FRAMES_IN_FLIGHT];

    // secondary CB cache
    std::vector<VkCommandBuffer> m_geomCmd;
    std::vector<uint64_t>        m_geomHash;
    std::vector<uint32_t>        m_cachedVerts;
    std::vector<uint32_t>        m_cachedCalls;
    std::vector<bool>            m_cachedWireframe;

    bool           m_useMeshBatch = true;

    // timing & stats
    Time* m_time = nullptr;
    bool                  m_wireframeOn = false;
    bool                  m_enableFrustumCulling = false;
    static constexpr int  ROLLING_AVG_SAMPLES = 120;
    std::deque<float>     m_fpsSamples;
    std::deque<float>     m_cpuSamples;

    // camera
    Camera        m_camera;
};
