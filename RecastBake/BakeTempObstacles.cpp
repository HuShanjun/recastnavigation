#include "Bake.h"
#include "BakeCommon.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include "RecastBakeCore/TileCacheCompression.h"
#include "RecastBakeCore/TileRasterizer.h"

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr int MAX_LAYERS = 32;
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int TILECACHESET_VERSION = 1;

struct MeshProcess : dtTileCacheMeshProcess
{
	InputGeom* inputGeometry = nullptr;

	~MeshProcess() override = default;

	void init(InputGeom* geom) { inputGeometry = geom; }

	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override
	{
		for (int i = 0; i < params->polyCount; ++i)
		{
			if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA)
			{
				polyAreas[i] = SAMPLE_POLYAREA_GROUND;
			}

			if (polyAreas[i] == SAMPLE_POLYAREA_GROUND || polyAreas[i] == SAMPLE_POLYAREA_GRASS ||
			    polyAreas[i] == SAMPLE_POLYAREA_ROAD)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_WALK;
			}
			else if (polyAreas[i] == SAMPLE_POLYAREA_WATER)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_SWIM;
			}
			else if (polyAreas[i] == SAMPLE_POLYAREA_DOOR)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
			}
		}

		if (inputGeometry)
		{
			params->offMeshConVerts = inputGeometry->offmeshConnVerts.data();
			params->offMeshConRad = inputGeometry->offmeshConnRadius.data();
			params->offMeshConDir = inputGeometry->offmeshConnBidirectional.data();
			params->offMeshConAreas = inputGeometry->offmeshConnArea.data();
			params->offMeshConFlags = inputGeometry->offmeshConnFlags.data();
			params->offMeshConUserID = inputGeometry->offmeshConnId.data();
			params->offMeshConCount = static_cast<int>(inputGeometry->offmeshConnArea.size());
		}
	}
};

struct TileCacheData
{
	unsigned char* data = nullptr;
	int dataSize = 0;
};

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

int rasterizeTileLayers(
	InputGeom& geom,
	const BakeConfig& bakeCfg,
	BuildContext& ctx,
	const int tileX,
	const int tileY,
	const rcConfig& cfg,
	TileCacheData* tiles,
	const int maxTiles)
{
	if (geom.mesh.getVertCount() == 0 || geom.partitionedMesh.tris.empty())
	{
		ctx.log(RC_LOG_ERROR, "buildTile: Input mesh is not specified.");
		return 0;
	}

	const float* verts = geom.mesh.verts.data();
	const int nverts = geom.mesh.getVertCount();
	const PartitionedMesh& partitionedMesh = geom.partitionedMesh;

	rcConfig tcfg;
	computeTileConfig(cfg, tileX, tileY, tcfg);

	bool empty = false;
	rcHeightfield* solid = rasterizeTileHeightfield(&ctx, tcfg, verts, nverts, partitionedMesh, bakeCfg, &empty);
	if (!solid)
	{
		if (!empty)
		{
			ctx.log(RC_LOG_ERROR, "buildNavigation: Could not rasterize tile.");
		}
		return 0;
	}

	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf || !rcBuildCompactHeightfield(&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		rcFreeCompactHeightfield(chf);
		rcFreeHeightField(solid);
		return 0;
	}
	rcFreeHeightField(solid);

	if (!rcErodeWalkableArea(&ctx, tcfg.walkableRadius, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		rcFreeCompactHeightfield(chf);
		return 0;
	}

	for (ConvexVolume& vol : geom.convexVolumes)
	{
		rcMarkConvexPolyArea(&ctx, vol.verts, vol.nverts, vol.hmin, vol.hmax, static_cast<unsigned char>(vol.area), *chf);
	}

	std::vector<CompressedTileLayer> layers;
	if (!buildCompressedTileLayers(&ctx, *chf, tileX, tileY, tcfg.borderSize, tcfg.walkableHeight, layers))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build heighfield layers.");
		rcFreeCompactHeightfield(chf);
		return 0;
	}
	rcFreeCompactHeightfield(chf);

	int n = 0;
	for (size_t i = 0; i < layers.size(); ++i)
	{
		if (n < maxTiles)
		{
			tiles[n].data = layers[i].data;
			tiles[n].dataSize = layers[i].dataSize;
			++n;
		}
		else
		{
			dtFree(layers[i].data);
		}
	}

	return n;
}

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
} // namespace

bool bakeTempObstacles(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount)
{
	outTileCount = 0;

	if (geom.mesh.getVertCount() == 0)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: No vertices and triangles.");
		return false;
	}

	LinearAllocator tAllocator(32000);
	FastLZCompressor tCompressor;
	MeshProcess tMeshProcess;
	tMeshProcess.init(&geom);

	const float* minBounds = geom.getNavMeshBoundsMin();
	const float* maxBounds = geom.getNavMeshBoundsMax();
	int gw = 0;
	int gh = 0;
	rcCalcGridSize(minBounds, maxBounds, cfg.cellSize, &gw, &gh);
	const int ts = cfg.tileSize;
	const int tw = (gw + ts - 1) / ts;
	const int th = (gh + ts - 1) / ts;
	const int expectedLayers = cfg.expectedLayersPerTile > 0 ? cfg.expectedLayersPerTile : 4;

	int tileBits = rcMin(static_cast<int>(dtIlog2(dtNextPow2(static_cast<unsigned int>(tw * th * expectedLayers)))), 14);
	if (tileBits < 1)
	{
		tileBits = 1;
	}
	const int polyBits = 22 - tileBits;
	const int maxTiles = 1 << tileBits;
	const int maxPolysPerTile = 1 << polyBits;

	rcConfig rcCfg;
	fillRcConfigTiled(cfg, minBounds, maxBounds, rcCfg);

	dtTileCacheParams tcparams = {};
	rcVcopy(tcparams.orig, minBounds);
	tcparams.cs = cfg.cellSize;
	tcparams.ch = cfg.cellHeight;
	tcparams.width = cfg.tileSize;
	tcparams.height = cfg.tileSize;
	tcparams.walkableHeight = cfg.agentHeight;
	tcparams.walkableRadius = cfg.agentRadius;
	tcparams.walkableClimb = cfg.agentMaxClimb;
	tcparams.maxSimplificationError = cfg.edgeMaxError;
	tcparams.maxTiles = tw * th * expectedLayers;
	tcparams.maxObstacles = cfg.maxObstacles;

	dtTileCache* tileCache = dtAllocTileCache();
	if (!tileCache)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not allocate tile cache.");
		return false;
	}
	if (dtStatusFailed(tileCache->init(&tcparams, &tAllocator, &tCompressor, &tMeshProcess)))
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not init tile cache.");
		dtFreeTileCache(tileCache);
		return false;
	}

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not allocate navmesh.");
		dtFreeTileCache(tileCache);
		return false;
	}

	dtNavMeshParams params = {};
	rcVcopy(params.orig, minBounds);
	params.tileWidth = static_cast<float>(cfg.tileSize) * cfg.cellSize;
	params.tileHeight = static_cast<float>(cfg.tileSize) * cfg.cellSize;
	params.maxTiles = maxTiles;
	params.maxPolys = maxPolysPerTile;
	if (dtStatusFailed(navMesh->init(&params)))
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not init navmesh.");
		dtFreeNavMesh(navMesh);
		dtFreeTileCache(tileCache);
		return false;
	}

	ctx.log(RC_LOG_PROGRESS, "Building tile cache %d x %d (maxTiles=%d)...", tw, th, maxTiles);

	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			TileCacheData tiles[MAX_LAYERS] = {};
			const int ntiles = rasterizeTileLayers(geom, cfg, ctx, x, y, rcCfg, tiles, MAX_LAYERS);

			for (int i = 0; i < ntiles; ++i)
			{
				TileCacheData* tile = &tiles[i];
				const dtStatus status = tileCache->addTile(tile->data, tile->dataSize, DT_COMPRESSEDTILE_FREE_DATA, 0);
				if (dtStatusFailed(status))
				{
					dtFree(tile->data);
					tile->data = nullptr;
				}
			}
		}
	}

	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			tileCache->buildNavMeshTilesAt(x, y, navMesh);
		}
	}

	outTileCount = countTileCacheTiles(tileCache);
	if (outTileCount == 0)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: No tile-cache tiles were generated.");
		dtFreeNavMesh(navMesh);
		dtFreeTileCache(tileCache);
		return false;
	}

	if (!saveTileCacheSet(outPath, tileCache, navMesh))
	{
		ctx.log(RC_LOG_ERROR, "Failed to write '%s'", outPath);
		dtFreeNavMesh(navMesh);
		dtFreeTileCache(tileCache);
		return false;
	}

	dtFreeNavMesh(navMesh);
	dtFreeTileCache(tileCache);
	return true;
}
