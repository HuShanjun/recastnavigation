#include "RecastBakeCore/TileRasterizer.h"

#include "RecastBakeCore/TileCacheCompression.h"

#include "PartitionedMesh.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourStatus.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <cmath>
#include <cstring>

void fillRcConfigSolo(const BakeCoreParams& p, const float* bmin, const float* bmax, rcConfig& out)
{
	std::memset(&out, 0, sizeof(out));
	out.cs = p.cellSize;
	out.ch = p.cellHeight;
	out.walkableSlopeAngle = p.agentMaxSlope;
	out.walkableHeight = static_cast<int>(ceilf(p.agentHeight / out.ch));
	out.walkableClimb = static_cast<int>(floorf(p.agentMaxClimb / out.ch));
	out.walkableRadius = static_cast<int>(ceilf(p.agentRadius / out.cs));
	out.maxEdgeLen = static_cast<int>(p.edgeMaxLen / p.cellSize);
	out.maxSimplificationError = p.edgeMaxError;
	out.minRegionArea = static_cast<int>(rcSqr(p.regionMinSize));
	out.mergeRegionArea = static_cast<int>(rcSqr(p.regionMergeSize));
	out.maxVertsPerPoly = p.vertsPerPoly;
	out.detailSampleDist = p.detailSampleDist < 0.9f ? 0 : p.cellSize * p.detailSampleDist;
	out.detailSampleMaxError = p.cellHeight * p.detailSampleMaxError;
	rcVcopy(out.bmin, bmin);
	rcVcopy(out.bmax, bmax);
	rcCalcGridSize(out.bmin, out.bmax, out.cs, &out.width, &out.height);
}

void fillRcConfigTiled(const BakeCoreParams& p, const float* meshBmin, const float* meshBmax, rcConfig& out)
{
	std::memset(&out, 0, sizeof(out));
	out.cs = p.cellSize;
	out.ch = p.cellHeight;
	out.walkableSlopeAngle = p.agentMaxSlope;
	out.walkableHeight = static_cast<int>(std::ceil(p.agentHeight / out.ch));
	out.walkableClimb = static_cast<int>(std::floor(p.agentMaxClimb / out.ch));
	out.walkableRadius = static_cast<int>(std::ceil(p.agentRadius / out.cs));
	out.maxEdgeLen = static_cast<int>(p.edgeMaxLen / p.cellSize);
	out.maxSimplificationError = p.edgeMaxError;
	out.minRegionArea = static_cast<int>(rcSqr(p.regionMinSize));
	out.mergeRegionArea = static_cast<int>(rcSqr(p.regionMergeSize));
	out.maxVertsPerPoly = p.vertsPerPoly;
	out.tileSize = p.tileSize;
	out.borderSize = out.walkableRadius + 3;
	out.width = out.tileSize + out.borderSize * 2;
	out.height = out.tileSize + out.borderSize * 2;
	out.detailSampleDist = p.detailSampleDist < 0.9f ? 0.0f : p.cellSize * p.detailSampleDist;
	out.detailSampleMaxError = p.cellHeight * p.detailSampleMaxError;
	rcVcopy(out.bmin, meshBmin);
	rcVcopy(out.bmax, meshBmax);
}

void computeTileConfig(const rcConfig& baseCfg, const int tx, const int ty, rcConfig& outTileCfg)
{
	const float tcs = baseCfg.tileSize * baseCfg.cs;
	std::memcpy(&outTileCfg, &baseCfg, sizeof(outTileCfg));
	outTileCfg.bmin[0] = baseCfg.bmin[0] + tx * tcs;
	outTileCfg.bmin[1] = baseCfg.bmin[1];
	outTileCfg.bmin[2] = baseCfg.bmin[2] + ty * tcs;
	outTileCfg.bmax[0] = baseCfg.bmin[0] + (tx + 1) * tcs;
	outTileCfg.bmax[1] = baseCfg.bmax[1];
	outTileCfg.bmax[2] = baseCfg.bmin[2] + (ty + 1) * tcs;
	outTileCfg.bmin[0] -= static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmin[2] -= static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmax[0] += static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmax[2] += static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
}

rcHeightfield* rasterizeTileHeightfield(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts,
	const int nverts,
	const PartitionedMesh& partitioned,
	const BakeCoreParams& params,
	bool* outEmpty)
{
	if (outEmpty)
	{
		*outEmpty = false;
	}

	rcHeightfield* heightfield = rcAllocHeightfield();
	if (!heightfield ||
	    !rcCreateHeightfield(
			ctx, *heightfield, tileCfg.width, tileCfg.height, tileCfg.bmin, tileCfg.bmax, tileCfg.cs, tileCfg.ch))
	{
		rcFreeHeightField(heightfield);
		return nullptr;
	}

	unsigned char* triAreas = new unsigned char[partitioned.maxTrisPerChunk];

	float tbmin[2] = {tileCfg.bmin[0], tileCfg.bmin[2]};
	float tbmax[2] = {tileCfg.bmax[0], tileCfg.bmax[2]};
	std::vector<int> overlappingNodes;
	partitioned.GetNodesOverlappingRect(tbmin, tbmax, overlappingNodes);
	if (overlappingNodes.empty())
	{
		delete[] triAreas;
		rcFreeHeightField(heightfield);
		if (outEmpty)
		{
			*outEmpty = true;
		}
		return nullptr;
	}

	for (const int nodeIndex : overlappingNodes)
	{
		const PartitionedMesh::Node& node = partitioned.nodes[static_cast<size_t>(nodeIndex)];
		const int* tris = &partitioned.tris[static_cast<size_t>(node.triIndex) * 3];
		const int ntris = node.numTris;

		std::memset(triAreas, 0, static_cast<size_t>(ntris) * sizeof(unsigned char));
		rcMarkWalkableTriangles(ctx, tileCfg.walkableSlopeAngle, verts, nverts, tris, ntris, triAreas);
		if (!rcRasterizeTriangles(ctx, verts, nverts, tris, triAreas, ntris, *heightfield, tileCfg.walkableClimb))
		{
			delete[] triAreas;
			rcFreeHeightField(heightfield);
			return nullptr;
		}
	}
	delete[] triAreas;

	if (params.filterLowHangingObstacles)
	{
		rcFilterLowHangingWalkableObstacles(ctx, tileCfg.walkableClimb, *heightfield);
	}
	if (params.filterLedgeSpans)
	{
		rcFilterLedgeSpans(ctx, tileCfg.walkableHeight, tileCfg.walkableClimb, *heightfield);
	}
	if (params.filterWalkableLowHeightSpans)
	{
		rcFilterWalkableLowHeightSpans(ctx, tileCfg.walkableHeight, *heightfield);
	}

	return heightfield;
}

bool buildCompressedTileLayers(
	rcContext* ctx,
	rcCompactHeightfield& chf,
	const int tx,
	const int ty,
	const int borderSize,
	const int walkableHeight,
	std::vector<CompressedTileLayer>& out)
{
	out.clear();

	rcHeightfieldLayerSet* lset = rcAllocHeightfieldLayerSet();
	if (!lset)
	{
		return false;
	}
	if (!rcBuildHeightfieldLayers(ctx, chf, borderSize, walkableHeight, *lset))
	{
		rcFreeHeightfieldLayerSet(lset);
		return false;
	}

	FastLZCompressor comp;

	for (int i = 0; i < lset->nlayers; ++i)
	{
		const rcHeightfieldLayer* layer = &lset->layers[i];

		dtTileCacheLayerHeader header;
		header.magic = DT_TILECACHE_MAGIC;
		header.version = DT_TILECACHE_VERSION;
		header.tx = tx;
		header.ty = ty;
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

		unsigned char* data = nullptr;
		int dataSize = 0;
		const dtStatus status =
			dtBuildTileCacheLayer(&comp, &header, layer->heights, layer->areas, layer->cons, &data, &dataSize);
		if (dtStatusFailed(status))
		{
			for (CompressedTileLayer& produced : out)
			{
				dtFree(produced.data);
			}
			out.clear();
			rcFreeHeightfieldLayerSet(lset);
			return false;
		}

		out.push_back(CompressedTileLayer{data, dataSize});
	}

	rcFreeHeightfieldLayerSet(lset);
	return true;
}
