#include "TileCacheSupport.h"

#include "DetourStatus.h"

#include <cstdio>

namespace
{
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';  // TSET
constexpr int TILECACHESET_VERSION = 1;
constexpr int QUERY_NODE_COUNT = 2048;

struct TileCacheSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams meshParams;
	dtTileCacheParams cacheParams;
};

struct TileCacheTileHeader
{
	dtCompressedTileRef tileRef;
	int dataSize;
};

bool rebuildTouchedTiles(NavSession* session)
{
	bool upToDate = false;
	for (int i = 0; i < 256 && !upToDate; ++i)
	{
		const dtStatus status = session->tileCache->update(0.0f, session->navMesh, &upToDate);
		if (dtStatusFailed(status))
		{
			std::printf("ERROR: tileCache->update failed (status=0x%x)\n", status);
			return false;
		}
	}
	if (!upToDate)
	{
		std::printf("ERROR: tile cache did not finish rebuilding\n");
		return false;
	}
	return true;
}
}

bool requireTileCache(const NavSession* session)
{
	if (!requireLoaded(session))
	{
		return false;
	}
	if (session->kind != LoadedKind::TileCache || !session->tileCache)
	{
		std::printf(
			"ERROR: obstacles require tilecache. Current=%s. Load with: load tilecache <file>\n",
			loadedKindName(session->kind));
		return false;
	}
	return true;
}

bool loadTileCacheSet(NavSession* session, TileCacheContext* ctx, const char* path)
{
	FILE* file = std::fopen(path, "rb");
	if (!file)
	{
		std::printf("ERROR: cannot open '%s'\n", path);
		return false;
	}

	TileCacheSetHeader header;
	if (std::fread(&header, sizeof(header), 1, file) != 1)
	{
		std::printf("ERROR: failed to read tile-cache header\n");
		std::fclose(file);
		return false;
	}
	if (header.magic != TILECACHESET_MAGIC || header.version != TILECACHESET_VERSION)
	{
		std::printf("ERROR: not a TSET tilecache (use Temp Obstacles Save)\n");
		std::fclose(file);
		return false;
	}

	unloadNavSession(session);

	session->navMesh = dtAllocNavMesh();
	if (!session->navMesh || dtStatusFailed(session->navMesh->init(&header.meshParams)))
	{
		std::printf("ERROR: failed to init dtNavMesh\n");
		unloadNavSession(session);
		std::fclose(file);
		return false;
	}

	session->tileCache = dtAllocTileCache();
	if (!session->tileCache ||
	    dtStatusFailed(session->tileCache->init(&header.cacheParams, &ctx->allocator, &ctx->compressor, &ctx->meshProcess)))
	{
		std::printf("ERROR: failed to init dtTileCache\n");
		unloadNavSession(session);
		std::fclose(file);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		TileCacheTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile header %d\n", i);
			unloadNavSession(session);
			std::fclose(file);
			return false;
		}
		if (!tileHeader.tileRef || !tileHeader.dataSize)
		{
			break;
		}

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!data || std::fread(data, tileHeader.dataSize, 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile %d data\n", i);
			dtFree(data);
			unloadNavSession(session);
			std::fclose(file);
			return false;
		}

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus =
			session->tileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus))
		{
			dtFree(data);
			continue;
		}
		if (tileRef)
		{
			session->tileCache->buildNavMeshTile(tileRef, session->navMesh);
		}
	}
	std::fclose(file);

	session->query = dtAllocNavMeshQuery();
	if (!session->query || dtStatusFailed(session->query->init(session->navMesh, QUERY_NODE_COUNT)))
	{
		std::printf("ERROR: failed to init dtNavMeshQuery\n");
		unloadNavSession(session);
		return false;
	}

	int tileCount = 0;
	for (int i = 0; i < session->tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = session->tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize)
		{
			++tileCount;
		}
	}

	session->kind = LoadedKind::TileCache;
	session->loadedFile = path;
	std::printf(
		"Loaded tilecache '%s'  tiles=%d  maxObstacles=%d\n",
		path,
		tileCount,
		session->tileCache->getParams()->maxObstacles);
	printNavMeshBounds(session->navMesh);
	return true;
}

bool addBoxObstacle(NavSession* session, float x, float z, float width, float depth, float height)
{
	if (!requireTileCache(session))
	{
		return false;
	}

	const float halfExtents[3] = {dtMax(width, 2.0f), 1000.0f, dtMax(depth, 2.0f)};
	const float queryPos[3] = {x, 0.0f, z};
	dtQueryFilter filter;
	dtPolyRef groundRef = 0;
	float ground[3] = {x, 0.0f, z};
	session->query->findNearestPoly(queryPos, halfExtents, &filter, &groundRef, ground);
	if (!groundRef)
	{
		std::printf("WARN: no navmesh under obstacle, using Y=0\n");
		ground[0] = x;
		ground[1] = 0.0f;
		ground[2] = z;
	}

	const float bmin[3] = {x - width * 0.5f, ground[1], z - depth * 0.5f};
	const float bmax[3] = {x + width * 0.5f, ground[1] + height, z + depth * 0.5f};

	dtObstacleRef obstacleRef = 0;
	const dtStatus addStatus = session->tileCache->addBoxObstacle(bmin, bmax, &obstacleRef);
	if (dtStatusFailed(addStatus) || !obstacleRef)
	{
		std::printf("ERROR: addBoxObstacle failed (status=0x%x)\n", addStatus);
		return false;
	}

	std::printf(
		"Obstacle AABB min (%.3f, %.3f, %.3f) max (%.3f, %.3f, %.3f) ref=%u\n",
		bmin[0],
		bmin[1],
		bmin[2],
		bmax[0],
		bmax[1],
		bmax[2],
		static_cast<unsigned>(obstacleRef));

	if (!rebuildTouchedTiles(session))
	{
		return false;
	}
	std::printf("Touched tiles rebuilt.\n");
	return true;
}
