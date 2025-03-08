#pragma once

#include <vulkan/vulkan.h>
#include "ChunkManager.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include <vector>
#include <deque>
#include <cstdint>

// Forward declarations
class VulkanContext;
class ResourceManager;
class Renderer;

// If you’re doing ring-buffer chunk destruction:
struct QueuedChunkDestruction
{
    VkBuffer vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

class VoxelWorld
{
public:
    enum class MesherType { GREEDY, NAIVE };

    // TWO-ARG constructor
    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    // If you need a Renderer pointer for ring-buffer destruction:
    void setRenderer(Renderer* renderer);

    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    ChunkManager& getChunkManager() { return m_chunkManager; }

    // e.g. mesh timing
    static double getAvgMeshTime();

    // Mesher type selection
    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

private:
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr; // set later if needed

    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;

    static constexpr int VIEW_DISTANCE = 12;

    GreedyMesher     m_greedyMesher;
    NaiveMesher      m_naiveMesher;
    MesherType       m_currentMesherType = MesherType::GREEDY;

    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    // Internal helpers
    void loadOneChunk(const ChunkCoord& c);
    void unloadOneChunk(const ChunkCoord& c);

    void scheduleMeshingForDirtyChunks();
    void pollMeshBuildResults();
    void uploadMeshToChunk(class Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds
    );
    void destroyChunkBuffers(class Chunk& chunk);

    // Not used:
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);
};
