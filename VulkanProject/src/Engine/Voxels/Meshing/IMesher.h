#ifndef IMESHER_H
#define IMESHER_H

#include <vector>
#include <cstdint>
#include "../Chunk.h"
#include "../ChunkManager.h"

// ---------------------------------------------------------------------------
// Vertex
// ---------------------------------------------------------------------------
// Packed vertex layout:
//   • position : 3 × 32-bit floats  = 12 bytes
//   • color    : RGBA8 UNORM        =  4 bytes
//   • sunlight : 0-15 (4 bits)      |
//   • blocklight : 0-15 (4 bits)    | combined into one uint8_t each
//   • pad      : 2-byte padding to keep 4-byte alignment
//
// Stride: 20 bytes (aligned to 4) => remember to update pipeline bindings.
// ---------------------------------------------------------------------------
struct Vertex
{
    float     x, y, z;        ///< world-space position
    uint32_t  color;          ///< packed R8G8B8A8 UNORM
    uint8_t   sunLight;       ///< 0-15 baked skylight
    uint8_t   blockLight;     ///< 0-15 baked block-light
    uint16_t  _pad;           ///< align to 4-byte boundary

    Vertex(float X, float Y, float Z,
        uint32_t packedColor,
        uint8_t sun = 15,
        uint8_t block = 0)
        : x(X), y(Y), z(Z)
        , color(packedColor)
        , sunLight(sun)
        , blockLight(block)
        , _pad(0)
    {}
};

// ---------------------------------------------------------------------------
// IMesher interface
// ---------------------------------------------------------------------------
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
