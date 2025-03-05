#ifndef NAIVE_MESHER_H
#define NAIVE_MESHER_H

#include "IMesher.h"

class NaiveMesher : public IMesher {
public:
    virtual ~NaiveMesher() = default;

    // Mark the method as const to match the interface.
    virtual bool generateMesh(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager) const override;
};

#endif // NAIVE_MESHER_H
