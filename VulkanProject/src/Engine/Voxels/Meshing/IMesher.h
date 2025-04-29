#ifndef IMESHER_H
#define IMESHER_H

#include <vector>
#include <cstdint>
#include "../Chunk.h"
#include "../ChunkManager.h"

/* ──────────────────────────────────────────────────────────────────────────
   Unified packed-vertex layout (20 bytes):

      • vec3 position          : 12 bytes
      • RGBA8 colour           :  4 bytes
      • sunlight  (0-15)       :  1 byte
      • block-light (0-15)     :  1 byte
      • padding                :  2 bytes  (keeps 4-byte alignment)

   NOTE: GPU vertex-input bindings **must** use sizeof(Vertex)==20 and
         offset = offsetof(Vertex, color) for the colour attribute.
   ────────────────────────────────────────────────────────────────────────── */
struct Vertex
{
    float     x, y, z;          ///< world-space position
    uint32_t  color;            ///< packed R8G8B8A8 UNORM
    uint8_t   sunLight;         ///< baked skylight   (0-15)
    uint8_t   blockLight;       ///< baked blocklight (0-15)
    uint16_t  _pad;             ///< align to 4 bytes

    constexpr Vertex(float X, float Y, float Z,
        uint32_t packedColor,
        uint8_t  sun = 15,
        uint8_t  block = 0) noexcept
        : x(X), y(Y), z(Z),
        color(packedColor),
        sunLight(sun),
        blockLight(block),
        _pad(0) {}
};

static_assert(sizeof(Vertex) == 20,
    "Vertex stride must remain exactly 20 bytes!");

/* ────────────────────────────────────────────────────────────────────────── */
class IMesher
{
public:
    virtual ~IMesher() = default;

    virtual bool generateMesh(
        Chunk& chunk,
        int                  cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int                  offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager) const = 0;
};

#endif // IMESHER_H
