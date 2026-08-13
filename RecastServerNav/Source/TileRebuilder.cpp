#include "TileRebuilder.h"

#include "PartitionedMesh.h"
#include "TileCacheSupport.h"

#include "DetourCommon.h"
#include "DetourStatus.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr int MAX_LAYERS = 32;

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

void fillRcConfig(const ServerBakeParams& bake, const float* meshBmin, const float* meshBmax, rcConfig& cfg)
{
	std::memset(&cfg, 0, sizeof(cfg));
	cfg.cs = bake.cellSize;
	cfg.ch = bake.cellHeight;
	cfg.walkableSlopeAngle = bake.agentMaxSlope;
	cfg.walkableHeight = static_cast<int>(std::ceil(bake.agentHeight / cfg.ch));
	cfg.walkableClimb = static_cast<int>(std::floor(bake.agentMaxClimb / cfg.ch));
	cfg.walkableRadius = static_cast<int>(std::ceil(bake.agentRadius / cfg.cs));
	cfg.maxEdgeLen = static_cast<int>(bake.edgeMaxLen / bake.cellSize);
	cfg.maxSimplificationError = bake.edgeMaxError;
	cfg.minRegionArea = static_cast<int>(rcSqr(bake.regionMinSize));
	cfg.mergeRegionArea = static_cast<int>(rcSqr(bake.regionMergeSize));
	cfg.maxVertsPerPoly = bake.vertsPerPoly;
	cfg.tileSize = bake.tileSize;
	cfg.borderSize = cfg.walkableRadius + 3;
	cfg.width = cfg.tileSize + cfg.borderSize * 2;
	cfg.height = cfg.tileSize + cfg.borderSize * 2;
	cfg.detailSampleDist = bake.detailSampleDist < 0.9f ? 0.0f : bake.cellSize * bake.detailSampleDist;
	cfg.detailSampleMaxError = bake.cellHeight * bake.detailSampleMaxError;
	rcVcopy(cfg.bmin, meshBmin);
	rcVcopy(cfg.bmax, meshBmax);
}
} // namespace

bool rebuildTileLayers(const TileRebuildInput& in, TileRebuildOutput& out, rcContext* ctx)
{
	out.ok = false;
	out.layers.clear();

	if (!ctx || !in.verts || in.nverts <= 0 || !in.partitioned || !in.bake || !in.meshBmin || !in.meshBmax)
	{
		return false;
	}
	if (in.partitioned->tris.empty() || in.partitioned->maxTrisPerChunk <= 0)
	{
		return false;
	}

	rcConfig cfg;
	fillRcConfig(*in.bake, in.meshBmin, in.meshBmax, cfg);

	FastLZCompressor comp;
	RasterizationContext rasterContext;

	const float* verts = in.verts;
	const int nverts = in.nverts;
	const PartitionedMesh& partitionedMesh = *in.partitioned;
	const int tileX = in.tx;
	const int tileY = in.ty;

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
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Out of memory 'solid'.");
		return false;
	}
	if (!rcCreateHeightfield(
			ctx,
			*rasterContext.solid,
			tcfg.width,
			tcfg.height,
			tcfg.bmin,
			tcfg.bmax,
			tcfg.cs,
			tcfg.ch))
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Could not create solid heightfield.");
		return false;
	}

	rasterContext.triAreas = new unsigned char[partitionedMesh.maxTrisPerChunk];
	if (!rasterContext.triAreas)
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Out of memory 'triAreas' (%d).", partitionedMesh.maxTrisPerChunk);
		return false;
	}

	float tbmin[2] = {tcfg.bmin[0], tcfg.bmin[2]};
	float tbmax[2] = {tcfg.bmax[0], tcfg.bmax[2]};
	std::vector<int> overlappingNodes;
	partitionedMesh.GetNodesOverlappingRect(tbmin, tbmax, overlappingNodes);
	if (overlappingNodes.empty())
	{
		// Empty tile is a successful no-op (no layers).
		out.ok = true;
		return true;
	}

	for (int nodeIndex : overlappingNodes)
	{
		const PartitionedMesh::Node& node = partitionedMesh.nodes[static_cast<size_t>(nodeIndex)];
		const int* tris = &partitionedMesh.tris[static_cast<size_t>(node.triIndex) * 3];
		const int ntris = node.numTris;

		std::memset(rasterContext.triAreas, 0, static_cast<size_t>(ntris) * sizeof(unsigned char));
		rcMarkWalkableTriangles(ctx, tcfg.walkableSlopeAngle, verts, nverts, tris, ntris, rasterContext.triAreas);
		if (!rcRasterizeTriangles(
				ctx,
				verts,
				nverts,
				tris,
				rasterContext.triAreas,
				ntris,
				*rasterContext.solid,
				tcfg.walkableClimb))
		{
			return false;
		}
	}

	if (in.bake->filterLowHangingObstacles)
	{
		rcFilterLowHangingWalkableObstacles(ctx, tcfg.walkableClimb, *rasterContext.solid);
	}
	if (in.bake->filterLedgeSpans)
	{
		rcFilterLedgeSpans(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *rasterContext.solid);
	}
	if (in.bake->filterWalkableLowHeightSpans)
	{
		rcFilterWalkableLowHeightSpans(ctx, tcfg.walkableHeight, *rasterContext.solid);
	}

	rasterContext.chf = rcAllocCompactHeightfield();
	if (!rasterContext.chf)
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Out of memory 'chf'.");
		return false;
	}
	if (!rcBuildCompactHeightfield(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *rasterContext.solid, *rasterContext.chf))
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Could not build compact data.");
		return false;
	}

	if (!rcErodeWalkableArea(ctx, tcfg.walkableRadius, *rasterContext.chf))
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Could not erode.");
		return false;
	}

	// Permanent AABB blockouts (after erode, before layers) — matches Demo convex-volume mark timing.
	if (in.boxes && in.boxCount > 0)
	{
		for (int i = 0; i < in.boxCount; ++i)
		{
			rcMarkBoxArea(ctx, in.boxes[i].bmin, in.boxes[i].bmax, RC_NULL_AREA, *rasterContext.chf);
		}
	}

	rasterContext.lset = rcAllocHeightfieldLayerSet();
	if (!rasterContext.lset)
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Out of memory 'lset'.");
		return false;
	}
	if (!rcBuildHeightfieldLayers(ctx, *rasterContext.chf, tcfg.borderSize, tcfg.walkableHeight, *rasterContext.lset))
	{
		ctx->log(RC_LOG_ERROR, "rebuildTileLayers: Could not build heightfield layers.");
		return false;
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
			return false;
		}
	}

	out.layers.reserve(static_cast<size_t>(rasterContext.ntiles));
	for (int i = 0; i < rasterContext.ntiles; ++i)
	{
		TileCacheData& tile = rasterContext.tiles[i];
		if (!tile.data || tile.dataSize <= 0)
		{
			continue;
		}
		std::vector<unsigned char> blob(static_cast<size_t>(tile.dataSize));
		std::memcpy(blob.data(), tile.data, static_cast<size_t>(tile.dataSize));
		out.layers.push_back(std::move(blob));
		dtFree(tile.data);
		tile.data = nullptr;
		tile.dataSize = 0;
	}

	out.ok = true;
	return true;
}
