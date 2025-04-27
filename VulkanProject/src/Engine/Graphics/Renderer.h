#pragma once
// ───────────────────────────────────────────────────────────────────────────
// Renderer.h – batch‑rendering voxel renderer with background CB builder
//              [2025‑04‑27]   timeline‑semaphore & parallel‑submit overhaul
// ───────────────────────────────────────────────────────────────────────────
#include <deque>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>

#include "Engine/Graphics/Frustum.h"
#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"

// ─── forward declarations ─────────────────────────────────────────────────
class Window;
class VulkanContext;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Time;
class UIRenderer;
class SwapChain;

// ─── uniform payload ──────────────────────────────────────────────────────
struct MVPBlock { glm::mat4 mvp; };

// ─── per‑flight resources ─────────────────────────────────────────────────
struct FrameResources
{
    // NOTE: With timeline semaphores we need only *one* semaphore per frame,
    //       whose counter value we signal / wait.  We keep the old binary
    //       pair for backwards compatibility until all callers migrate.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;   // binary (legacy)
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;   // binary (legacy)

    /* NEW — timeline semaphore: a single handle whose counter we increment
       once per submission; acquire / present wait on specific values. */
    VkSemaphore     timelineSemaphore = VK_NULL_HANDLE;
    uint64_t        timelineValue = 0;               // last signalled

    VkFence         inFlightFence = VK_NULL_HANDLE;  // CPU‑side reset
};
static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
static constexpr int DESTROY_LATENCY = 3;

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
    /* ------------------------------------------------------------
       Mesh‑batch helper (unchanged; definition at bottom of header)
       ------------------------------------------------------------ */
    struct MeshBatch
    {
        VkBuffer       vbo = VK_NULL_HANDLE;
        VkBuffer       ibo = VK_NULL_HANDLE;
        VkDeviceMemory vboMemory = VK_NULL_HANDLE;
        VkDeviceMemory iboMemory = VK_NULL_HANDLE;

        VkDeviceSize   vboSize = 0, iboSize = 0;   // total bytes allocated
        VkDeviceSize   vboUsed = 0, iboUsed = 0;   // bytes used this build

        void ensureCapacity(Renderer* owner,
            VkDeviceSize wantVbo,
            VkDeviceSize wantIbo);
        inline void reset() { vboUsed = iboUsed = 0; }

        uint32_t appendChunk(Renderer* owner,
            VkBuffer srcVbo, VkDeviceSize srcVboBytes,
            VkBuffer srcIbo, VkDeviceSize srcIboBytes);
    };

    // ──────────────────────────────────────────────────────────────────
    // Background geometry builder (records secondary CBs off‑thread)
    // ──────────────────────────────────────────────────────────────────
    struct GeometryJob
    {
        Frustum   frustum;
        bool      useCulling = true;
        uint32_t  imgIdx = 0;  // swap‑chain image this CB targets
        bool      wantWire = false;
    };

    class GeometryBuilder
    {
    public:
        explicit GeometryBuilder(Renderer* owner);
        ~GeometryBuilder();

        void submit(const GeometryJob& job);
        VkCommandBuffer fetchFinished(uint32_t imgIdx,
            uint32_t& outVerts,
            uint32_t& outCalls,
            uint64_t& outHash);
    private:
        void threadMain();
        Renderer* m_owner;
        std::thread                 m_thread;
        std::mutex                  m_mutex;
        std::condition_variable     m_cv;

        std::deque<GeometryJob>     m_jobs;
        bool                        m_exit = false;

        struct FinishedCB
        {
            VkCommandBuffer cmd;
            uint32_t verts, calls;
            uint32_t imgIdx;
            uint64_t hash;
            bool     wire;
        };
        std::deque<FinishedCB>      m_done;

        VkCommandPool               m_cmdPool = VK_NULL_HANDLE;
    };

    // ------------------------------------------------------------------
    // geometry CB builder (legacy sync path)
    // ------------------------------------------------------------------
    uint64_t buildGeometryCB(uint32_t imgIdx,
        const Frustum& fr,
        bool           useCull,
        uint32_t& outVerts,
        uint32_t& outCalls);

    // ------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------
    void freeDeferredResources();
    void addSample(std::deque<float>& buf, float v);
    float computeAverage(const std::deque<float>& buf);

    void createMVPUniformBuffer();
    void updateMVP();
    void recreateSwapChain();

    /* NEW – timeline‑semaphore helpers */
    void createSyncObjects();
    void destroySyncObjects();

    void createBuffer(VkDeviceSize,
        VkBufferUsageFlags,
        VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

    // ------------------------------------------------------------------
    // members
    // ------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;
    UIRenderer* m_uiRenderer = nullptr;
    SwapChain* m_swapChain = nullptr;

    // uniform resources …
    VkBuffer            m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool    m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet     m_mvpDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;

    // per‑flight & deferred frees
    FrameResources      m_frames[MAX_FRAMES_IN_FLIGHT];
    int                 m_currentFrame = 0;
    std::vector<QueuedChunkDestruction>
        m_deferredFrees[MAX_FRAMES_IN_FLIGHT + DESTROY_LATENCY];

    // mesh‑batches per‑flight
    MeshBatch           m_batches[MAX_FRAMES_IN_FLIGHT];

    // secondary CB caches (output from GeometryBuilder)
    std::vector<VkCommandBuffer> m_geomCmd;
    std::vector<uint64_t>        m_geomHash;
    std::vector<uint32_t>        m_cachedVerts;
    std::vector<uint32_t>        m_cachedCalls;
    std::vector<bool>            m_cachedWireframe;

    // async builder instance
    std::unique_ptr<GeometryBuilder> m_geoBuilder;

    bool                m_useMeshBatch = true;

    // timing & UI stats
    Time* m_time = nullptr;
    bool                m_wireframeOn = false;
    bool                m_enableFrustumCulling = false;
    static constexpr int ROLLING_AVG_SAMPLES = 120;
    std::deque<float>   m_fpsSamples;
    std::deque<float>   m_cpuSamples;

    Camera              m_camera;

    // feature toggles
    bool                m_useTimelineSemaphores = true; // compile‑time opt‑out
};
