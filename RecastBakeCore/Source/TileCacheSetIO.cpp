#include "RecastBakeCore/TileCacheSetIO.h"

#include "DetourAlloc.h"
#include "DetourStatus.h"

#include <cstdio>
#include <cstring>

namespace
{
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int TILECACHESET_VERSION = 1;

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
} // namespace

bool saveTileCacheSet(const char* path, const dtTileCache* tileCache, const dtNavMesh* navMesh)
{
	if (!tileCache || !navMesh)
	{
		return false;
	}

	FILE* fp = std::fopen(path, "wb");
	if (!fp)
	{
		return false;
	}

	TileCacheSetHeader header;
	header.magic = TILECACHESET_MAGIC;
	header.version = TILECACHESET_VERSION;
	header.numTiles = 0;
	for (int i = 0; i < tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = tileCache->getTile(i);
		if (!tile || !tile->header || !tile->dataSize)
		{
			continue;
		}
		header.numTiles++;
	}
	std::memcpy(&header.cacheParams, tileCache->getParams(), sizeof(dtTileCacheParams));
	std::memcpy(&header.meshParams, navMesh->getParams(), sizeof(dtNavMeshParams));
	std::fwrite(&header, sizeof(TileCacheSetHeader), 1, fp);

	for (int i = 0; i < tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = tileCache->getTile(i);
		if (!tile || !tile->header || !tile->dataSize)
		{
			continue;
		}

		TileCacheTileHeader tileHeader;
		tileHeader.tileRef = tileCache->getTileRef(tile);
		tileHeader.dataSize = tile->dataSize;
		std::fwrite(&tileHeader, sizeof(tileHeader), 1, fp);
		std::fwrite(tile->data, tile->dataSize, 1, fp);
	}

	std::fclose(fp);
	return true;
}

int countTileCacheTiles(const dtTileCache* tileCache)
{
	if (!tileCache)
	{
		return 0;
	}
	int count = 0;
	for (int i = 0; i < tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize)
		{
			++count;
		}
	}
	return count;
}

bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache)
{
	*outNavMesh = nullptr;
	*outTileCache = nullptr;

	FILE* file = std::fopen(path, "rb");
	if (!file) { std::printf("ERROR: cannot open '%s'\n", path); return false; }

	TileCacheSetHeader header;
	if (std::fread(&header, sizeof(header), 1, file) != 1) { std::fclose(file); return false; }
	if (header.magic != TILECACHESET_MAGIC || header.version != TILECACHESET_VERSION)
	{
		std::printf("ERROR: not a RecastDemo tile-cache file (use Temp Obstacles -> Save)\n");
		std::fclose(file);
		return false;
	}

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh || dtStatusFailed(navMesh->init(&header.meshParams)))
	{
		std::fclose(file);
		dtFreeNavMesh(navMesh);
		return false;
	}

	dtTileCache* tileCache = dtAllocTileCache();
	if (!tileCache || dtStatusFailed(tileCache->init(&header.cacheParams, alloc, compressor, meshProcess)))
	{
		std::fclose(file);
		dtFreeTileCache(tileCache);
		dtFreeNavMesh(navMesh);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		TileCacheTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::fclose(file);
			dtFreeTileCache(tileCache);
			dtFreeNavMesh(navMesh);
			return false;
		}
		if (!tileHeader.tileRef || !tileHeader.dataSize) break;

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!data || std::fread(data, tileHeader.dataSize, 1, file) != 1)
		{
			dtFree(data);
			std::fclose(file);
			dtFreeTileCache(tileCache);
			dtFreeNavMesh(navMesh);
			return false;
		}

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus = tileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus)) { dtFree(data); continue; }
		if (tileRef) tileCache->buildNavMeshTile(tileRef, navMesh);
	}

	std::fclose(file);
	*outNavMesh = navMesh;
	*outTileCache = tileCache;
	return true;
}
