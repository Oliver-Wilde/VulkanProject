#ifndef IMESHER_H
#define IMESHER_H

#include <vector>
#include "../Chunk.h"
#include "../ChunkManager.h"

// Forward declaration for Vertex (or include the header where Vertex is defined)
struct Vertex;

class IMesher {
public:
    virtual ~IMesher() = default;

    // Add const here so derived classes must also mark the method as const.
    virtual bool generateMesh(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager) const = 0;
};

#endif // IMESHER_H
