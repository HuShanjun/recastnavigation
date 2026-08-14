#include "TileRebuilder.h"

#include "PartitionedMesh.h"

#include "RecastBakeCore/TileRasterizer.h"

#include "DetourCommon.h"
#include "DetourStatus.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <vector>

namespace
{
bool aabbOverlaps2D(const float bmin[3], const float bmax[3], const float tbmin[3], const float tbmax[3])
{
	return bmin[0] <= tbmax[0] && bmax[0] >= tbmin[0] && bmin[2] <= tbmax[2] && bmax[2] >= tbmin[2];
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
	fillRcConfigTiled(*in.bake, in.meshBmin, in.meshBmax, cfg);
	rcConfig tcfg;
	computeTileConfig(cfg, in.tx, in.ty, tcfg);

	bool empty = false;
	rcHeightfield* solid = rasterizeTileHeightfield(ctx, tcfg, in.verts, in.nverts, *in.partitioned, *in.bake, &empty);
	if (!solid)
	{
		if (empty && in.meshObjectCount == 0)
		{
			out.ok = true;
			return true; // empty tile stays a successful no-op
		}
		if (empty)
		{
			// No terrain overlap, but mesh objects may still overlap this tile — build an
			// empty heightfield to rasterize into instead of bailing out.
			solid = rcAllocHeightfield();
			if (!solid ||
				!rcCreateHeightfield(ctx, *solid, tcfg.width, tcfg.height, tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch))
			{
				rcFreeHeightField(solid);
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	// Mesh-object triangles must land in the same heightfield BEFORE the 3 obstacle filters
	// (unlike PermanentBox, which marks null-area AFTER erode below). rasterizeTileHeightfield()
	// already ran the filters over the terrain-only spans, so re-run them once more below after
	// adding mesh spans; the filters are pure/idempotent over current span state.
	bool rasterizedAnyMesh = false;
	for (int i = 0; i < in.meshObjectCount; ++i)
	{
		const PermanentMeshObject& obj = in.meshObjects[i];
		if (!aabbOverlaps2D(obj.bmin, obj.bmax, tcfg.bmin, tcfg.bmax))
		{
			continue;
		}
		if (!rasterizeExtraTriangles(
				ctx, tcfg, obj.verts.data(), static_cast<int>(obj.verts.size() / 3), obj.tris.data(),
				static_cast<int>(obj.tris.size() / 3), *in.bake, *solid))
		{
			rcFreeHeightField(solid);
			return false;
		}
		rasterizedAnyMesh = true;
	}

	if (rasterizedAnyMesh)
	{
		if (in.bake->filterLowHangingObstacles)
		{
			rcFilterLowHangingWalkableObstacles(ctx, tcfg.walkableClimb, *solid);
		}
		if (in.bake->filterLedgeSpans)
		{
			rcFilterLedgeSpans(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid);
		}
		if (in.bake->filterWalkableLowHeightSpans)
		{
			rcFilterWalkableLowHeightSpans(ctx, tcfg.walkableHeight, *solid);
		}
	}
	else if (empty)
	{
		// Still no geometry at all in this tile.
		rcFreeHeightField(solid);
		out.ok = true;
		return true;
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
