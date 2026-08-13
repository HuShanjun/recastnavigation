#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <fastlz.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';  // TSET
constexpr int TILECACHESET_VERSION = 1;
constexpr int MAX_POLYS = 256;
constexpr int MAX_STRAIGHT_PATH = 256;
constexpr int QUERY_NODE_COUNT = 2048;
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

struct FastLZCompressor : dtTileCacheCompressor
{
	int maxCompressedSize(const int bufferSize) override
	{
		return static_cast<int>(static_cast<float>(bufferSize) * 1.05f);
	}

	dtStatus compress(
		const unsigned char* buffer,
		const int bufferSize,
		unsigned char* compressed,
		const int /*maxCompressedSize*/,
		int* compressedSize) override
	{
		*compressedSize = fastlz_compress(buffer, bufferSize, compressed);
		return DT_SUCCESS;
	}

	dtStatus decompress(
		const unsigned char* compressed,
		const int compressedSize,
		unsigned char* buffer,
		const int maxBufferSize,
		int* bufferSize) override
	{
		*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
		return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
	}
};

struct LinearAllocator : dtTileCacheAlloc
{
	unsigned char* buffer = nullptr;
	size_t capacity = 0;
	size_t top = 0;
	size_t high = 0;

	explicit LinearAllocator(const size_t cap)
	{
		buffer = static_cast<unsigned char*>(dtAlloc(cap, DT_ALLOC_PERM));
		capacity = cap;
	}

	~LinearAllocator() override { dtFree(buffer); }

	void reset() override
	{
		high = dtMax(high, top);
		top = 0;
	}

	void* alloc(const size_t size) override
	{
		if (!buffer || top + size > capacity)
		{
			return nullptr;
		}
		unsigned char* mem = &buffer[top];
		top += size;
		return mem;
	}

	void free(void* /*ptr*/) override {}
};

struct MeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override
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
};

struct Scene
{
	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	dtNavMeshQuery* query = nullptr;
	LinearAllocator allocator{512 * 1024};
	FastLZCompressor compressor;
	MeshProcess meshProcess;
};

void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <tilecache.bin> <houseX> <houseZ> <width> <depth> <height>\n"
		"  %s <tilecache.bin> <houseX> <houseZ> <width> <depth> <height> <startX> <startZ> <endX> <endZ>\n"
		"\n"
		"  tilecache.bin  RecastDemo Temp Obstacles Save (all_tiles_tilecache.bin).\n"
		"  house*         AABB footprint on XZ, height along Recast Y.\n"
		"  path coords    Optional; prints the path before and after placing the house.\n",
		exe,
		exe);
}

void destroyScene(Scene* scene)
{
	dtFreeNavMeshQuery(scene->query);
	dtFreeTileCache(scene->tileCache);
	dtFreeNavMesh(scene->navMesh);
	scene->query = nullptr;
	scene->tileCache = nullptr;
	scene->navMesh = nullptr;
}

bool loadTileCache(Scene* scene, const char* path)
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
		std::printf("ERROR: not a RecastDemo tile-cache file (use Temp Obstacles -> Save)\n");
		std::fclose(file);
		return false;
	}

	scene->navMesh = dtAllocNavMesh();
	if (!scene->navMesh || dtStatusFailed(scene->navMesh->init(&header.meshParams)))
	{
		std::printf("ERROR: failed to init dtNavMesh\n");
		std::fclose(file);
		return false;
	}

	scene->tileCache = dtAllocTileCache();
	if (!scene->tileCache ||
	    dtStatusFailed(scene->tileCache->init(&header.cacheParams, &scene->allocator, &scene->compressor, &scene->meshProcess)))
	{
		std::printf("ERROR: failed to init dtTileCache\n");
		std::fclose(file);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		TileCacheTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile header %d\n", i);
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
			std::fclose(file);
			return false;
		}

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus = scene->tileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus))
		{
			dtFree(data);
			continue;
		}
		if (tileRef)
		{
			scene->tileCache->buildNavMeshTile(tileRef, scene->navMesh);
		}
	}

	std::fclose(file);

	scene->query = dtAllocNavMeshQuery();
	if (!scene->query || dtStatusFailed(scene->query->init(scene->navMesh, QUERY_NODE_COUNT)))
	{
		std::printf("ERROR: failed to init dtNavMeshQuery\n");
		return false;
	}

	int tileCount = 0;
	for (int i = 0; i < scene->tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = scene->tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize)
		{
			++tileCount;
		}
	}
	std::printf("Loaded tile-cache tiles=%d  maxObstacles=%d\n", tileCount, scene->tileCache->getParams()->maxObstacles);
	return true;
}

bool findPath(Scene* scene, const float* start, const float* end, const char* label)
{
	const float halfExtents[3] = {4.0f, 1000.0f, 4.0f};
	dtQueryFilter filter;

	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	float startNearest[3];
	float endNearest[3];
	scene->query->findNearestPoly(start, halfExtents, &filter, &startRef, startNearest);
	scene->query->findNearestPoly(end, halfExtents, &filter, &endRef, endNearest);
	if (!startRef || !endRef)
	{
		std::printf("%s: no poly at start or end\n", label);
		return false;
	}

	std::vector<dtPolyRef> polys(MAX_POLYS);
	int npolys = 0;
	const dtStatus pathStatus =
		scene->query->findPath(startRef, endRef, startNearest, endNearest, &filter, polys.data(), &npolys, MAX_POLYS);
	if (dtStatusFailed(pathStatus) || npolys == 0)
	{
		std::printf("%s: findPath failed (blocked or disconnected)\n", label);
		return false;
	}

	std::vector<float> straight(MAX_STRAIGHT_PATH * 3);
	int nstraight = 0;
	scene->query->findStraightPath(
		startNearest,
		endNearest,
		polys.data(),
		npolys,
		straight.data(),
		nullptr,
		nullptr,
		&nstraight,
		MAX_STRAIGHT_PATH);

	float length = 0.0f;
	for (int i = 1; i < nstraight; ++i)
	{
		length += dtVdist(&straight[(i - 1) * 3], &straight[i * 3]);
	}

	const bool partial = (pathStatus & DT_PARTIAL_RESULT) != 0;
	std::printf(
		"%s: corners=%d length=%.3f%s\n",
		label,
		nstraight,
		length,
		partial ? " (partial)" : "");
	for (int i = 0; i < nstraight; ++i)
	{
		const float* p = &straight[i * 3];
		std::printf("  %d  %.3f  %.3f  %.3f\n", i, p[0], p[1], p[2]);
	}
	return !partial;
}

bool rebuildTouchedTiles(Scene* scene)
{
	bool upToDate = false;
	for (int i = 0; i < 256 && !upToDate; ++i)
	{
		const dtStatus status = scene->tileCache->update(0.0f, scene->navMesh, &upToDate);
		if (dtStatusFailed(status))
		{
			std::printf("ERROR: tileCache->update failed (status=0x%x). Allocator may be too small.\n", status);
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

bool placeHouse(Scene* scene, float houseX, float houseZ, float width, float depth, float height)
{
	const float halfExtents[3] = {dtMax(width, 2.0f), 1000.0f, dtMax(depth, 2.0f)};
	const float queryPos[3] = {houseX, 0.0f, houseZ};
	dtQueryFilter filter;
	dtPolyRef groundRef = 0;
	float ground[3] = {houseX, 0.0f, houseZ};
	scene->query->findNearestPoly(queryPos, halfExtents, &filter, &groundRef, ground);
	if (!groundRef)
	{
		std::printf("WARN: no navmesh under the house, using Y=0\n");
		ground[0] = houseX;
		ground[1] = 0.0f;
		ground[2] = houseZ;
	}

	const float bmin[3] = {houseX - width * 0.5f, ground[1], houseZ - depth * 0.5f};
	const float bmax[3] = {houseX + width * 0.5f, ground[1] + height, houseZ + depth * 0.5f};

	dtObstacleRef obstacleRef = 0;
	const dtStatus addStatus = scene->tileCache->addBoxObstacle(bmin, bmax, &obstacleRef);
	if (dtStatusFailed(addStatus) || !obstacleRef)
	{
		std::printf("ERROR: addBoxObstacle failed (status=0x%x). Check maxObstacles.\n", addStatus);
		return false;
	}

	std::printf(
		"House AABB  min (%.3f, %.3f, %.3f)  max (%.3f, %.3f, %.3f)  ref=%u\n",
		bmin[0],
		bmin[1],
		bmin[2],
		bmax[0],
		bmax[1],
		bmax[2],
		static_cast<unsigned>(obstacleRef));

	if (!rebuildTouchedTiles(scene))
	{
		return false;
	}
	std::printf("Touched tiles rebuilt, navmesh updated.\n");
	return true;
}
}

int main(int argc, char* argv[])
{
	if (argc != 7 && argc != 11)
	{
		printUsage(argv[0]);
		return 1;
	}

	const char* tileCachePath = argv[1];
	const float houseX = std::strtof(argv[2], nullptr);
	const float houseZ = std::strtof(argv[3], nullptr);
	const float houseW = std::strtof(argv[4], nullptr);
	const float houseD = std::strtof(argv[5], nullptr);
	const float houseH = std::strtof(argv[6], nullptr);

	const bool hasPath = argc == 11;
	float start[3] = {0, 0, 0};
	float end[3] = {0, 0, 0};
	if (hasPath)
	{
		start[0] = std::strtof(argv[7], nullptr);
		start[2] = std::strtof(argv[8], nullptr);
		end[0] = std::strtof(argv[9], nullptr);
		end[2] = std::strtof(argv[10], nullptr);
	}

	Scene scene;
	if (!loadTileCache(&scene, tileCachePath))
	{
		destroyScene(&scene);
		return 1;
	}

	if (hasPath)
	{
		std::printf("\n-- before house --\n");
		findPath(&scene, start, end, "path");
	}

	std::printf("\n-- place house --\n");
	if (!placeHouse(&scene, houseX, houseZ, houseW, houseD, houseH))
	{
		destroyScene(&scene);
		return 1;
	}

	if (hasPath)
	{
		std::printf("\n-- after house --\n");
		findPath(&scene, start, end, "path");
	}

	destroyScene(&scene);
	return 0;
}
