#ifndef IMESHER_H
#define IMESHER_H

#include <vector>
#include <cstdint>
#include "../Chunk.h"
#include "../ChunkManager.h"

// The actual struct definition here:
struct Vertex
{
    float x, y, z;
    float r, g, b;

    Vertex(float X, float Y, float Z, float R, float G, float B)
        : x(X), y(Y), z(Z), r(R), g(G), b(B)
    {}
};

class IMesher
{
public:
    virtual ~IMesher() = default;

    virtual bool generateMesh(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    ) const = 0;
};

#endif // IMESHER_H
