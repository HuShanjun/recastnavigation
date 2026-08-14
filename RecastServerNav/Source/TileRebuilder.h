#pragma once

#include "BakeParams.h"

#include <vector>

struct PartitionedMesh;
class rcContext;

struct TileRebuildInput
{
	const float* verts = nullptr;
	int nverts = 0;
	const PartitionedMesh* partitioned = nullptr;
	const ServerBakeParams* bake = nullptr;
	const float* meshBmin = nullptr;
	const float* meshBmax = nullptr;
	const PermanentBox* boxes = nullptr;
	int boxCount = 0;
	const PermanentMeshObject* meshObjects = nullptr;
	int meshObjectCount = 0;
	int tx = 0;
	int ty = 0;
};

struct TileRebuildOutput
{
	bool ok = false;
	/// One compressed TileCache layer blob per entry (addTile-ready).
	std::vector<std::vector<unsigned char>> layers;
};

/// Rasterize one tile (tx,ty), mark permanent AABBs as null area after erode,
/// build heightfield layers and FastLZ-compress them for dtTileCache::addTile.
bool rebuildTileLayers(const TileRebuildInput& in, TileRebuildOutput& out, rcContext* ctx);
