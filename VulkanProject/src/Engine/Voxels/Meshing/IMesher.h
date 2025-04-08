#ifndef IMESHER_H
#define IMESHER_H

#include <vector>
#include <cstdint>
#include "../Chunk.h"
#include "../ChunkManager.h"

// A packed vertex with position (x,y,z) and color in RGBA8 format.
struct Vertex
{
    float x, y, z;      // 12 bytes for position
    uint32_t color;     // 4 bytes for packed color (R8G8B8A8, etc.)

    // Constructor takes position + a packed color integer
    Vertex(float X, float Y, float Z, uint32_t packedColor)
        : x(X), y(Y), z(Z), color(packedColor)
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
