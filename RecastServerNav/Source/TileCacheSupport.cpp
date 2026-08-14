#include "TileCacheSupport.h"

#include "RecastBakeCore/TileCacheSetIO.h"

#include "DetourAlloc.h"
#include "DetourNavMeshBuilder.h"
#include "DetourStatus.h"

#include <cstdio>
#include <cstring>

namespace
{
constexpr unsigned short POLYFLAGS_WALK = 1;
} // namespace

void MeshProcess::process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags)
{
	for (int i = 0; i < params->polyCount; ++i)
	{
		if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA)
		{
			polyAreas[i] = 0;
		}
		polyFlags[i] = POLYFLAGS_WALK;
	}
}

void destroyTileCacheRuntime(TileCacheRuntime& rt)
{
	dtFreeTileCache(rt.tileCache);
	dtFreeNavMesh(rt.navMesh);
	rt.tileCache = nullptr;
	rt.navMesh = nullptr;
	rt.allocator.reset();
}

bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path)
{
	destroyTileCacheRuntime(rt);

	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	if (!loadTileCacheSet(path, &rt.allocator, &rt.compressor, &rt.meshProcess, &navMesh, &tileCache))
	{
		std::printf("ERROR: failed to load tile-cache set '%s'\n", path);
		return false;
	}
	rt.navMesh = navMesh;
	rt.tileCache = tileCache;

	int tileCount = 0;
	for (int i = 0; i < rt.tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = rt.tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize) ++tileCount;
	}
	std::printf("Loaded tile-cache tiles=%d  maxObstacles=%d\n", tileCount, rt.tileCache->getParams()->maxObstacles);
	return true;
}
