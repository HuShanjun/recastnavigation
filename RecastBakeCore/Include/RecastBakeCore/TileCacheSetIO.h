#pragma once

#include "DetourNavMesh.h"
#include "DetourTileCache.h"

bool saveTileCacheSet(const char* path, const dtTileCache* tileCache, const dtNavMesh* navMesh);
int countTileCacheTiles(const dtTileCache* tileCache);

bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache);
