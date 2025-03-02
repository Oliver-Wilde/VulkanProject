//#include "Chunk.h"
//#include "ChunkManager.h"
//#include "VoxelTypeRegistry.h"
//#include "VoxelType.h"
//#include "Vertex.h"
//#include <vector>
//#include <cstdint>
//#include <iostream>
//
///**
// * Determines if the voxel ID is solid by checking VoxelTypeRegistry,
// * used in naive adjacency checks.
// */
//static bool isSolidID(int voxelID)
//{
//    if (voxelID < 0) return false; // negative => treat as air
//    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
//    return vt.isSolid;
//}
//
///**
// * Old method: check if local coords (x,y,z) in chunk or neighbor are solid
// * by returning (blockID > 0) && isSolidID(blockID).
// * This is used by the naive adjacency approach below.
// */
//bool isSolidGlobal(
//    const Chunk& currentChunk,
//    int cx, int cy, int cz,
//    int x, int y, int z,
//    const ChunkManager& manager)
//{
//    // If in-range => check current chunk's data
//    if (x >= 0 && x < Chunk::SIZE_X &&
//        y >= 0 && y < Chunk::SIZE_Y &&
//        z >= 0 && z < Chunk::SIZE_Z)
//    {
//        int id = currentChunk.getBlock(x, y, z);
//        return (id > 0) && isSolidID(id);
//    }
//    else
//    {
//        // Out-of-bounds => neighbor chunk
//        int nx = cx, ny = cy, nz = cz;
//        int localX = x, localY = y, localZ = z;
//
//        // shift coords to neighbor
//        if (localX < 0) {
//            nx -= 1;
//            localX += Chunk::SIZE_X;
//        }
//        else if (localX >= Chunk::SIZE_X) {
//            nx += 1;
//            localX -= Chunk::SIZE_X;
//        }
//        if (localY < 0) {
//            ny -= 1;
//            localY += Chunk::SIZE_Y;
//        }
//        else if (localY >= Chunk::SIZE_Y) {
//            ny += 1;
//            localY -= Chunk::SIZE_Y;
//        }
//        if (localZ < 0) {
//            nz -= 1;
//            localZ += Chunk::SIZE_Z;
//        }
//        else if (localZ >= Chunk::SIZE_Z) {
//            nz += 1;
//            localZ -= Chunk::SIZE_Z;
//        }
//
//        const Chunk* neighbor = manager.getChunk(nx, ny, nz);
//        if (!neighbor) {
//            // no neighbor => treat as air
//            return false;
//        }
//        int blockID = neighbor->getBlock(localX, localY, localZ);
//        return (blockID > 0) && isSolidID(blockID);
//    }
//}
//
////*
//// * Naive mesh generation with adjacency checks, but now we do "boundary merging"
//// * by comparing the neighbor's block ID vs. our own.
//// //
//void ChunkMesher::generateMeshNaive(
//    const Chunk& chunk,
//    int cx, int cy, int cz,
//    std::vector<Vertex>& outVertices,
//    std::vector<uint32_t>& outIndices,
//    int offsetX, int offsetY, int offsetZ,
//    const ChunkManager& manager)
//{
//    outVertices.clear();
//    outIndices.clear();
//
//    for (int x = 0; x < Chunk::SIZE_X; x++)
//    {
//        for (int y = 0; y < Chunk::SIZE_Y; y++)
//        {
//            for (int z = 0; z < Chunk::SIZE_Z; z++)
//            {
//                int voxelID = chunk.getBlock(x, y, z);
//                if (voxelID <= 0) continue; // skip air
//
//                const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
//                float r = vt.color.r;
//                float g = vt.color.g;
//                float b = vt.color.b;
//
//                float baseX = float(x + offsetX);
//                float baseY = float(y + offsetY);
//                float baseZ = float(z + offsetZ);
//
//                // +X face => if neighbor ID != voxelID
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x + 1, y, z, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//                // -X face
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x - 1, y, z, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//                // +Y face
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y + 1, z, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//                // -Y face
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y - 1, z, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//                // +Z face
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y, z + 1, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//                // -Z face
//                {
//                    int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y, z - 1, manager);
//                    if (neighborID != voxelID) {
//                        int startIdx = (int)outVertices.size();
//                        outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
//                        outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));
//
//                        outIndices.push_back(startIdx + 0);
//                        outIndices.push_back(startIdx + 1);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 2);
//                        outIndices.push_back(startIdx + 3);
//                        outIndices.push_back(startIdx + 0);
//                    }
//                }
//            }
//        }
//    }
//}
//
///**
// * A simpler test method ignoring adjacency with neighbor chunks.
// * (No changes needed for boundary merging, but kept for reference.)
// */
//void ChunkMesher::generateMeshNaiveTest(
//    const Chunk& chunk,
//    std::vector<Vertex>& outVerts,
//    std::vector<uint32_t>& outInds,
//    int offsetX, int offsetY, int offsetZ)
//{
//    outVerts.clear();
//    outInds.clear();
//
//    // ... same as before (no neighbor logic here) ...
//    for (int x = 0; x < Chunk::SIZE_X; x++)
//    {
//        for (int y = 0; y < Chunk::SIZE_Y; y++)
//        {
//            for (int z = 0; z < Chunk::SIZE_Z; z++)
//            {
//                int blockID = chunk.getBlock(x, y, z);
//                if (blockID <= 0) continue;
//
//                float r = 0.5f, g = 0.5f, b = 0.5f; // Hard-coded color
//                float bx = float(x + offsetX);
//                float by = float(y + offsetY);
//                float bz = float(z + offsetZ);
//
//                // +X face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//                // -X face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//                // +Y face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//                // -Y face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//                // +Z face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
//                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//                // -Z face
//                {
//                    int startIdx = (int)outVerts.size();
//                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
//                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));
//
//                    outInds.push_back(startIdx + 0);
//                    outInds.push_back(startIdx + 1);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 2);
//                    outInds.push_back(startIdx + 3);
//                    outInds.push_back(startIdx + 0);
//                }
//            }
//        }
//    }
//}


