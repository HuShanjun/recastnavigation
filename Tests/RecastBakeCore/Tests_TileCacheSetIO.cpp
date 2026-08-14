#include "RecastBakeCore/TileCacheSetIO.h"
#include "RecastBakeCore/TileCacheCompression.h"
#include "DetourNavMesh.h"
#include "DetourTileCache.h"
#include <catch2/catch_amalgamated.hpp>
#include <cstdio>

namespace
{
struct NoOpMeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams*, unsigned char*, unsigned short*) override {}
};
} // namespace

TEST_CASE("TileCacheSetIO save/load round-trip preserves tile count", "[RecastBakeCore]")
{
	dtTileCacheParams tcparams = {};
	tcparams.cs = 0.3f;
	tcparams.ch = 0.2f;
	tcparams.width = 48;
	tcparams.height = 48;
	tcparams.walkableHeight = 2.0f;
	tcparams.walkableRadius = 0.6f;
	tcparams.walkableClimb = 0.9f;
	tcparams.maxSimplificationError = 1.3f;
	tcparams.maxTiles = 32;
	tcparams.maxObstacles = 16;

	LinearAllocator alloc(32000);
	FastLZCompressor comp;
	NoOpMeshProcess meshProcess;

	dtTileCache* tileCache = dtAllocTileCache();
	REQUIRE(dtStatusSucceed(tileCache->init(&tcparams, &alloc, &comp, &meshProcess)));

	dtNavMesh* navMesh = dtAllocNavMesh();
	dtNavMeshParams navParams = {};
	navParams.tileWidth = tcparams.width * tcparams.cs;
	navParams.tileHeight = tcparams.height * tcparams.cs;
	navParams.maxTiles = 32;
	navParams.maxPolys = 1024;
	REQUIRE(dtStatusSucceed(navMesh->init(&navParams)));

	REQUIRE(countTileCacheTiles(tileCache) == 0);

	const char* path = "roundtrip_test.tset";
	REQUIRE(saveTileCacheSet(path, tileCache, navMesh));

	LinearAllocator loadAlloc(32000);
	FastLZCompressor loadComp;
	NoOpMeshProcess loadMeshProcess;
	dtNavMesh* loadedNavMesh = nullptr;
	dtTileCache* loadedTileCache = nullptr;
	REQUIRE(loadTileCacheSet(path, &loadAlloc, &loadComp, &loadMeshProcess, &loadedNavMesh, &loadedTileCache));
	REQUIRE(countTileCacheTiles(loadedTileCache) == countTileCacheTiles(tileCache));

	std::remove(path);
	dtFreeTileCache(tileCache);
	dtFreeNavMesh(navMesh);
	dtFreeTileCache(loadedTileCache);
	dtFreeNavMesh(loadedNavMesh);
}
