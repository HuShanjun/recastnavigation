#include "TileCacheSupport.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMeshBuilder.h"
#include "DetourStatus.h"

#include <fastlz.h>

#include <cstdio>
#include <cstring>

namespace
{
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T'; // TSET
constexpr int TILECACHESET_VERSION = 1;
constexpr unsigned short POLYFLAGS_WALK = 1;

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

int FastLZCompressor::maxCompressedSize(const int bufferSize)
{
	return static_cast<int>(static_cast<float>(bufferSize) * 1.05f);
}

dtStatus FastLZCompressor::compress(
	const unsigned char* buffer,
	const int bufferSize,
	unsigned char* compressed,
	const int /*maxCompressedSize*/,
	int* compressedSize)
{
	*compressedSize = fastlz_compress(buffer, bufferSize, compressed);
	return DT_SUCCESS;
}

dtStatus FastLZCompressor::decompress(
	const unsigned char* compressed,
	const int compressedSize,
	unsigned char* buffer,
	const int maxBufferSize,
	int* bufferSize)
{
	*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
	return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
}

LinearAllocator::LinearAllocator(const size_t cap)
{
	buffer = static_cast<unsigned char*>(dtAlloc(cap, DT_ALLOC_PERM));
	capacity = cap;
}

LinearAllocator::~LinearAllocator()
{
	dtFree(buffer);
}

void LinearAllocator::reset()
{
	high = dtMax(high, top);
	top = 0;
}

void* LinearAllocator::alloc(const size_t size)
{
	if (!buffer || top + size > capacity)
	{
		return nullptr;
	}
	unsigned char* mem = &buffer[top];
	top += size;
	return mem;
}

void LinearAllocator::free(void* /*ptr*/) {}

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
		std::printf("ERROR: not a RecastDemo tile-cache file (use Temp Obstacles -> Save)\n");
		std::fclose(file);
		return false;
	}

	rt.navMesh = dtAllocNavMesh();
	if (!rt.navMesh || dtStatusFailed(rt.navMesh->init(&header.meshParams)))
	{
		std::printf("ERROR: failed to init dtNavMesh\n");
		std::fclose(file);
		destroyTileCacheRuntime(rt);
		return false;
	}

	rt.tileCache = dtAllocTileCache();
	if (!rt.tileCache ||
	    dtStatusFailed(rt.tileCache->init(&header.cacheParams, &rt.allocator, &rt.compressor, &rt.meshProcess)))
	{
		std::printf("ERROR: failed to init dtTileCache\n");
		std::fclose(file);
		destroyTileCacheRuntime(rt);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		TileCacheTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile header %d\n", i);
			std::fclose(file);
			destroyTileCacheRuntime(rt);
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
			std::fclose(file);
			destroyTileCacheRuntime(rt);
			return false;
		}

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus = rt.tileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus))
		{
			dtFree(data);
			continue;
		}
		if (tileRef)
		{
			rt.tileCache->buildNavMeshTile(tileRef, rt.navMesh);
		}
	}

	std::fclose(file);

	int tileCount = 0;
	for (int i = 0; i < rt.tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = rt.tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize)
		{
			++tileCount;
		}
	}
	std::printf("Loaded tile-cache tiles=%d  maxObstacles=%d\n", tileCount, rt.tileCache->getParams()->maxObstacles);
	return true;
}
