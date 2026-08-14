#pragma once

#include "RecastBakeCore/TileCacheCompression.h"

#include "DetourNavMesh.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <cstddef>

struct MeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override;
};

struct TileCacheRuntime
{
	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	LinearAllocator allocator{512 * 1024};
	FastLZCompressor compressor;
	MeshProcess meshProcess;
};

bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path);
void destroyTileCacheRuntime(TileCacheRuntime& rt);
