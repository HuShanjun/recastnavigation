#include "TileRebuilder.h"

#include "PartitionedMesh.h"

#include "RecastBakeCore/TileRasterizer.h"

#include "DetourCommon.h"
#include "DetourStatus.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <vector>

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
	fillRcConfigTiled(*in.bake, in.meshBmin, in.meshBmax, cfg);
	rcConfig tcfg;
	computeTileConfig(cfg, in.tx, in.ty, tcfg);

	bool empty = false;
	rcHeightfield* solid = rasterizeTileHeightfield(ctx, tcfg, in.verts, in.nverts, *in.partitioned, *in.bake, &empty);
	if (!solid)
	{
		if (empty)
		{
			out.ok = true;
			return true; // empty tile stays a successful no-op
		}
		return false;
	}

	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf || !rcBuildCompactHeightfield(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid, *chf))
	{
		rcFreeHeightField(solid);
		rcFreeCompactHeightfield(chf);
		return false;
	}
	rcFreeHeightField(solid);

	if (!rcErodeWalkableArea(ctx, tcfg.walkableRadius, *chf))
	{
		rcFreeCompactHeightfield(chf);
		return false;
	}

	// Permanent AABB blockouts (after erode, before layers) — matches Demo convex-volume mark timing.
	if (in.boxes && in.boxCount > 0)
	{
		for (int i = 0; i < in.boxCount; ++i)
		{
			rcMarkBoxArea(ctx, in.boxes[i].bmin, in.boxes[i].bmax, RC_NULL_AREA, *chf);
		}
	}

	std::vector<CompressedTileLayer> layers;
	if (!buildCompressedTileLayers(ctx, *chf, in.tx, in.ty, tcfg.borderSize, tcfg.walkableHeight, layers))
	{
		rcFreeCompactHeightfield(chf);
		return false;
	}
	rcFreeCompactHeightfield(chf);

	out.layers.reserve(layers.size());
	for (CompressedTileLayer& l : layers)
	{
		out.layers.emplace_back(l.data, l.data + l.dataSize);
		dtFree(l.data);
	}
	out.ok = true;
	return true;
}
