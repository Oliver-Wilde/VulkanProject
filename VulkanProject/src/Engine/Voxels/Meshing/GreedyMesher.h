#ifndef GREEDY_MESHER_H
#define GREEDY_MESHER_H

#include "IMesher.h"

class GreedyMesher : public IMesher {
public:
    virtual ~GreedyMesher() = default;

    // Add const here too.
    virtual bool generateMesh(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager) const override;
};

#endif // GREEDY_MESHER_H
