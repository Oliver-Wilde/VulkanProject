#pragma once

#include <deque>
#include <vector>            // for std::vector
#include <unordered_map>     // for std::unordered_map
#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>    // for MVPBlock
#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"  // for QueuedChunkDestruction

// Forward declarations
class Window;
class VulkanContext;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Camera;
class Time;
class UIRenderer;
class Chunk;

/**
 * A simple struct holding our MVP matrix.
 * This is what's placed in a uniform buffer for shaders.
 */
struct MVPBlock
{
    glm::mat4 mvp;
};

/**
 * Holds resources for each "frame in flight" (2 or 3).
 * This includes command buffers, semaphores, and fences.
 */
struct FrameResources
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

/// How many frames we keep in flight (e.g. double or triple buffering).
static const int MAX_FRAMES_IN_FLIGHT = 2;

/**
 * The Renderer class is responsible for:
 *   - Creating swap chain & related Vulkan resources
 *   - Managing pipelines, uniform buffers (MVP), etc.
 *   - Providing a renderFrame() function that draws the voxel world and the ImGui UI
 */
class Renderer
{
public:
    /**
     * Constructor
     */
    Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld);

    /**
     * Destructor
     */
    ~Renderer();

    /**
     * Render one frame:
     *   - Acquire swap chain image
     *   - Record command buffer
     *   - Let UIRenderer handle ImGui UI
     *   - Present the result
     */
    void renderFrame();

    /**
     * Set the camera used for MVP calculations.
     */
    void setCamera(const Camera& cam);

    /**
     * Toggle wireframe pipeline usage.
     */
    void toggleWireframe();

    /**
     * Enable or disable frustum culling.
     */
    void enableFrustumCulling(bool enable);

    /**
     * Provide a pointer to your Time object, if you want dt/fps in the renderer.
     */
    void setTime(Time* time);

    /**
     * Enqueue chunk buffers for deferred free on next use of this frame index.
     */
    void enqueueDeferredDestroy(const QueuedChunkDestruction& qcd);

private:
    /**
     * Creates the uniform buffer for MVP (model-view-projection),
     * plus its descriptor set/pool.
     */
    void createMVPUniformBuffer();

    /**
     * Updates the MVP uniform buffer with the current camera view/projection.
     */
    void updateMVP();

    /**
     * Recreates the swap chain (and relevant resources).
     */
    void recreateSwapChain();

    /**
     * Helper for creating any Vulkan buffer.
     */
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    /**
     * Finds a memory type from the GPU that fits 'filter' and 'props'.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    /**
     * Adds a new sample to a rolling-average buffer (for e.g. FPS or CPU usage).
     */
    void addSample(std::deque<float>& buffer, float value);

    /**
     * Computes the average of all samples in a deque.
     */
    float computeAverage(const std::deque<float>& buffer);

    /**
     * Called each frame at start to free chunk buffers queued from previous usage.
     */
    void freeDeferredResources();

    // ------------------------------------------------------------------------
    // NEW: GPU Occlusion Query Fields & Methods
    // ------------------------------------------------------------------------

    /// Maximum number of occlusion queries we support at once.
    static const uint32_t MAX_OCCLUSION_QUERIES = 4096;

    /// A query pool for GPU occlusion queries.
    VkQueryPool m_occlusionQueryPool = VK_NULL_HANDLE;

    /// Raw results (number of samples that passed) for each query.
    std::vector<uint64_t> m_queryResults;

    /// A boolean per query to mark whether it passed (>0 samples) or not.
    std::vector<bool> m_chunkVisibility;

    /// Maps a Chunk pointer to its query index. If we run out, we skip.
    std::unordered_map<Chunk*, uint32_t> m_chunkQueryIndices;

    /**
     * Gather results from last frame's occlusion queries.
     * Interprets them into m_chunkVisibility.
     */
    void gatherOcclusionResults();

    /**
     * Renders bounding boxes in a pass with occlusion queries.
     * Typically you'd do this in a small or separate depth pass.
     */
    void renderOcclusionPass(VkCommandBuffer cmdBuf);

    /**
     * Actually draws the bounding box for a chunk, enclosed by Begin/EndQuery.
     */
    void drawBoundingBox(Chunk* chunk, VkCommandBuffer cmdBuf);

    /**
     * Record that a given chunk is associated with a particular query index.
     */
    void setQueryIndexForChunk(Chunk* chunk, uint32_t index);

    /**
     * Retrieve the query index for a chunk, or -1 if none.
     */
    int getQueryIndexForChunk(Chunk* chunk);

private:
    // Core references
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    // Manager objects
    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;
    UIRenderer* m_uiRenderer = nullptr;

    // Swap chain + MVP data
    class SwapChain* m_swapChain = nullptr;
    VkBuffer            m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool    m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet     m_mvpDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;

    // For ring-buffer chunk destruction
    std::vector<QueuedChunkDestruction> m_deferredFrees[MAX_FRAMES_IN_FLIGHT];

    // Per-frame resources
    FrameResources m_frames[MAX_FRAMES_IN_FLIGHT];
    int            m_currentFrame = 0; // which frame in flight

    // If using Time for dt/fps
    Time* m_time = nullptr;

    // Toggles
    bool m_wireframeOn = false;
    bool m_enableFrustumCulling = false;

    // Rolling-average data
    static const int ROLLING_AVG_SAMPLES = 120;
    std::deque<float> m_fpsSamples;
    std::deque<float> m_cpuSamples;

    // Current camera
    Camera m_camera;
};
