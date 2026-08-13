#include "Bake.h"
#include "BakeCommon.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <fastlz.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr int MAX_LAYERS = 32;
constexpr int TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int TILECACHESET_VERSION = 1;

struct FastLZCompressor : dtTileCacheCompressor
{
	~FastLZCompressor() override = default;

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

	explicit LinearAllocator(const size_t cap) { resize(cap); }

	~LinearAllocator() override { dtFree(buffer); }

	void resize(const size_t cap)
	{
		if (buffer)
		{
			dtFree(buffer);
		}
		buffer = static_cast<unsigned char*>(dtAlloc(cap, DT_ALLOC_PERM));
		capacity = cap;
	}

	void reset() override
	{
		high = dtMax(high, top);
		top = 0;
	}

	void* alloc(const size_t size) override
	{
		if (!buffer)
		{
			return nullptr;
		}
		if (top + size > capacity)
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

struct RasterizationContext
{
	rcHeightfield* solid = nullptr;
	unsigned char* triAreas = nullptr;
	rcHeightfieldLayerSet* lset = nullptr;
	rcCompactHeightfield* chf = nullptr;
	TileCacheData tiles[MAX_LAYERS]{};
	int ntiles = 0;

	~RasterizationContext()
	{
		rcFreeHeightField(solid);
		delete[] triAreas;
		rcFreeHeightfieldLayerSet(lset);
		rcFreeCompactHeightfield(chf);
		for (int i = 0; i < MAX_LAYERS; ++i)
		{
			dtFree(tiles[i].data);
			tiles[i].data = nullptr;
		}
	}
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

	FastLZCompressor comp;
	RasterizationContext rasterContext;

	const float* verts = geom.mesh.verts.data();
	const int nverts = geom.mesh.getVertCount();
	const PartitionedMesh& partitionedMesh = geom.partitionedMesh;

	const float tcs = cfg.tileSize * cfg.cs;

	rcConfig tcfg;
	std::memcpy(&tcfg, &cfg, sizeof(tcfg));

	tcfg.bmin[0] = cfg.bmin[0] + tileX * tcs;
	tcfg.bmin[1] = cfg.bmin[1];
	tcfg.bmin[2] = cfg.bmin[2] + tileY * tcs;
	tcfg.bmax[0] = cfg.bmin[0] + (tileX + 1) * tcs;
	tcfg.bmax[1] = cfg.bmax[1];
	tcfg.bmax[2] = cfg.bmin[2] + (tileY + 1) * tcs;
	tcfg.bmin[0] -= static_cast<float>(tcfg.borderSize) * tcfg.cs;
	tcfg.bmin[2] -= static_cast<float>(tcfg.borderSize) * tcfg.cs;
	tcfg.bmax[0] += static_cast<float>(tcfg.borderSize) * tcfg.cs;
	tcfg.bmax[2] += static_cast<float>(tcfg.borderSize) * tcfg.cs;

	rasterContext.solid = rcAllocHeightfield();
	if (!rasterContext.solid)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'solid'.");
		return 0;
	}
	if (!rcCreateHeightfield(
			&ctx,
			*rasterContext.solid,
			tcfg.width,
			tcfg.height,
			tcfg.bmin,
			tcfg.bmax,
			tcfg.cs,
			tcfg.ch))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield.");
		return 0;
	}

	rasterContext.triAreas = new unsigned char[partitionedMesh.maxTrisPerChunk];
	if (!rasterContext.triAreas)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'triAreas' (%d).", partitionedMesh.maxTrisPerChunk);
		return 0;
	}

	float tbmin[2] = {tcfg.bmin[0], tcfg.bmin[2]};
	float tbmax[2] = {tcfg.bmax[0], tcfg.bmax[2]};
	std::vector<int> overlappingNodes;
	partitionedMesh.GetNodesOverlappingRect(tbmin, tbmax, overlappingNodes);
	if (overlappingNodes.empty())
	{
		return 0;
	}

	for (int nodeIndex : overlappingNodes)
	{
		const PartitionedMesh::Node& node = partitionedMesh.nodes[nodeIndex];
		const int* tris = &partitionedMesh.tris[static_cast<size_t>(node.triIndex) * 3];
		const int ntris = node.numTris;

		std::memset(rasterContext.triAreas, 0, ntris * sizeof(unsigned char));
		rcMarkWalkableTriangles(&ctx, tcfg.walkableSlopeAngle, verts, nverts, tris, ntris, rasterContext.triAreas);
		if (!rcRasterizeTriangles(
				&ctx,
				verts,
				nverts,
				tris,
				rasterContext.triAreas,
				ntris,
				*rasterContext.solid,
				tcfg.walkableClimb))
		{
			return 0;
		}
	}

	if (bakeCfg.filterLowHangingObstacles)
	{
		rcFilterLowHangingWalkableObstacles(&ctx, tcfg.walkableClimb, *rasterContext.solid);
	}
	if (bakeCfg.filterLedgeSpans)
	{
		rcFilterLedgeSpans(&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *rasterContext.solid);
	}
	if (bakeCfg.filterWalkableLowHeightSpans)
	{
		rcFilterWalkableLowHeightSpans(&ctx, tcfg.walkableHeight, *rasterContext.solid);
	}

	rasterContext.chf = rcAllocCompactHeightfield();
	if (!rasterContext.chf)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'chf'.");
		return 0;
	}
	if (!rcBuildCompactHeightfield(
			&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *rasterContext.solid, *rasterContext.chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		return 0;
	}

	if (!rcErodeWalkableArea(&ctx, tcfg.walkableRadius, *rasterContext.chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		return 0;
	}

	for (ConvexVolume& vol : geom.convexVolumes)
	{
		rcMarkConvexPolyArea(
			&ctx, vol.verts, vol.nverts, vol.hmin, vol.hmax, static_cast<unsigned char>(vol.area), *rasterContext.chf);
	}

	rasterContext.lset = rcAllocHeightfieldLayerSet();
	if (!rasterContext.lset)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'lset'.");
		return 0;
	}
	if (!rcBuildHeightfieldLayers(&ctx, *rasterContext.chf, tcfg.borderSize, tcfg.walkableHeight, *rasterContext.lset))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build heighfield layers.");
		return 0;
	}

	rasterContext.ntiles = 0;
	for (int i = 0; i < rcMin(rasterContext.lset->nlayers, MAX_LAYERS); ++i)
	{
		TileCacheData* tile = &rasterContext.tiles[rasterContext.ntiles++];
		const rcHeightfieldLayer* layer = &rasterContext.lset->layers[i];

		dtTileCacheLayerHeader header;
		header.magic = DT_TILECACHE_MAGIC;
		header.version = DT_TILECACHE_VERSION;
		header.tx = tileX;
		header.ty = tileY;
		header.tlayer = i;
		dtVcopy(header.bmin, layer->bmin);
		dtVcopy(header.bmax, layer->bmax);
		header.width = static_cast<unsigned char>(layer->width);
		header.height = static_cast<unsigned char>(layer->height);
		header.minx = static_cast<unsigned char>(layer->minx);
		header.maxx = static_cast<unsigned char>(layer->maxx);
		header.miny = static_cast<unsigned char>(layer->miny);
		header.maxy = static_cast<unsigned char>(layer->maxy);
		header.hmin = static_cast<unsigned short>(layer->hmin);
		header.hmax = static_cast<unsigned short>(layer->hmax);

		dtStatus status =
			dtBuildTileCacheLayer(&comp, &header, layer->heights, layer->areas, layer->cons, &tile->data, &tile->dataSize);
		if (dtStatusFailed(status))
		{
			return 0;
		}
	}

	int n = 0;
	for (int i = 0; i < rcMin(rasterContext.ntiles, maxTiles); ++i)
	{
		tiles[n++] = rasterContext.tiles[i];
		rasterContext.tiles[i].data = nullptr;
		rasterContext.tiles[i].dataSize = 0;
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

	rcConfig rcCfg = {};
	rcCfg.cs = cfg.cellSize;
	rcCfg.ch = cfg.cellHeight;
	rcCfg.walkableSlopeAngle = cfg.agentMaxSlope;
	rcCfg.walkableHeight = static_cast<int>(ceilf(cfg.agentHeight / rcCfg.ch));
	rcCfg.walkableClimb = static_cast<int>(floorf(cfg.agentMaxClimb / rcCfg.ch));
	rcCfg.walkableRadius = static_cast<int>(ceilf(cfg.agentRadius / rcCfg.cs));
	rcCfg.maxEdgeLen = static_cast<int>(cfg.edgeMaxLen / cfg.cellSize);
	rcCfg.maxSimplificationError = cfg.edgeMaxError;
	rcCfg.minRegionArea = static_cast<int>(rcSqr(cfg.regionMinSize));
	rcCfg.mergeRegionArea = static_cast<int>(rcSqr(cfg.regionMergeSize));
	rcCfg.maxVertsPerPoly = cfg.vertsPerPoly;
	rcCfg.tileSize = cfg.tileSize;
	rcCfg.borderSize = rcCfg.walkableRadius + 3;
	rcCfg.width = rcCfg.tileSize + rcCfg.borderSize * 2;
	rcCfg.height = rcCfg.tileSize + rcCfg.borderSize * 2;
	rcCfg.detailSampleDist = cfg.detailSampleDist < 0.9f ? 0 : cfg.cellSize * cfg.detailSampleDist;
	rcCfg.detailSampleMaxError = cfg.cellHeight * cfg.detailSampleMaxError;
	rcVcopy(rcCfg.bmin, minBounds);
	rcVcopy(rcCfg.bmax, maxBounds);

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
